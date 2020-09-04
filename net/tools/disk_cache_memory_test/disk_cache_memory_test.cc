// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
ninja -C out/Testing disk_cache_memory_test

D:\dev\electron7\src>out\Testing\disk_cache_memory_test.exe --spec-1=block_file:disk_cache:xxx

*/

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/cache_type.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/disk_cache.h"
#include "net/disk_cache/simple/simple_backend_impl.h"
#include "net/disk_cache/simple/simple_index.h"


#include "net/disk_cache/blockfile/backend_impl.h"
#include "net/disk_cache/blockfile/file.h"
#include "net/disk_cache/cache_util.h"



#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "net/base/io_buffer.h"

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/timer/timer.h"
#include "build/build_config.h"

//-----------------------------------------------------------------------------
#include "net/http/http_cache.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"

///
/// 
//-----------------------------------------------------------------------------
// completion callback helper

// A helper class for completion callbacks, designed to make it easy to run
// tests involving asynchronous operations.  Just call WaitForResult to wait
// for the asynchronous operation to complete.  Uses a RunLoop to spin the
// current MessageLoop while waiting.  The callback must be invoked on the same
// thread WaitForResult is called on.
//
// NOTE: Since this runs a message loop to wait for the completion callback,
// there could be other side-effects resulting from WaitForResult.  For this
// reason, this class is probably not ideal for a general application.
//
namespace base {
class RunLoop;
}

namespace net {

class IOBuffer;

namespace internal {

class TestCompletionCallbackBaseInternal {
 public:
  bool have_result() const { return have_result_; }

 protected:
  TestCompletionCallbackBaseInternal();
  virtual ~TestCompletionCallbackBaseInternal();

  void DidSetResult();
  void WaitForResult();

 private:
  // RunLoop.  Only non-NULL during the call to WaitForResult, so the class is
  // reusable.
  std::unique_ptr<base::RunLoop> run_loop_;
  bool have_result_;

  DISALLOW_COPY_AND_ASSIGN(TestCompletionCallbackBaseInternal);
};

template <typename R>
struct NetErrorIsPendingHelper {
  bool operator()(R status) const { return status == ERR_IO_PENDING; }
};

template <typename R, typename IsPendingHelper = NetErrorIsPendingHelper<R>>
class TestCompletionCallbackTemplate
    : public TestCompletionCallbackBaseInternal {
 public:
  virtual ~TestCompletionCallbackTemplate() override {}

  R WaitForResult() {
    TestCompletionCallbackBaseInternal::WaitForResult();
    return std::move(result_);
  }

  R GetResult(R result) {
    IsPendingHelper check_pending;
    if (!check_pending(result))
      return std::move(result);
    return WaitForResult();
  }

 protected:
  TestCompletionCallbackTemplate() : result_(R()) {}

  // Override this method to gain control as the callback is running.
  virtual void SetResult(R result) {
    result_ = std::move(result);
    DidSetResult();
  }

 private:
  R result_;

  DISALLOW_COPY_AND_ASSIGN(TestCompletionCallbackTemplate);
};


void TestCompletionCallbackBaseInternal::DidSetResult() {
  have_result_ = true;
  if (run_loop_)
    run_loop_->Quit();
}

void TestCompletionCallbackBaseInternal::WaitForResult() {
  DCHECK(!run_loop_);
  if (!have_result_) {
    run_loop_ = std::make_unique<base::RunLoop>(
        base::RunLoop::Type::kNestableTasksAllowed);
    run_loop_->Run();
    run_loop_.reset();
    DCHECK(have_result_);
  }
  have_result_ = false;  // Auto-reset for next callback.
}

TestCompletionCallbackBaseInternal::TestCompletionCallbackBaseInternal()
    : have_result_(false) {}

TestCompletionCallbackBaseInternal::~TestCompletionCallbackBaseInternal() =
    default;


}  // namespace internal


// Base class overridden by custom implementations of TestCompletionCallback.
typedef internal::TestCompletionCallbackTemplate<int>
    TestCompletionCallbackBase;

typedef internal::TestCompletionCallbackTemplate<int64_t>
    TestInt64CompletionCallbackBase;

class TestCompletionCallback : public TestCompletionCallbackBase {
 public:
  TestCompletionCallback() {}
  ~TestCompletionCallback() override;

  CompletionOnceCallback callback() {
    return base::BindOnce(&TestCompletionCallback::SetResult,
                          base::Unretained(this));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestCompletionCallback);
};

class TestInt64CompletionCallback : public TestInt64CompletionCallbackBase {
 public:
  TestInt64CompletionCallback() {}
  ~TestInt64CompletionCallback() override;

  Int64CompletionOnceCallback callback() {
    return base::BindOnce(&TestInt64CompletionCallback::SetResult,
                          base::Unretained(this));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestInt64CompletionCallback);
};

// Makes sure that the buffer is not referenced when the callback runs.
class ReleaseBufferCompletionCallback : public TestCompletionCallback {
 public:
  explicit ReleaseBufferCompletionCallback(IOBuffer* buffer);
  ~ReleaseBufferCompletionCallback() override;

 private:
  void SetResult(int result) override;

  IOBuffer* buffer_;
  DISALLOW_COPY_AND_ASSIGN(ReleaseBufferCompletionCallback);
};


TestCompletionCallback::~TestCompletionCallback() = default;

TestInt64CompletionCallback::~TestInt64CompletionCallback() = default;

ReleaseBufferCompletionCallback::ReleaseBufferCompletionCallback(
    IOBuffer* buffer)
    : buffer_(buffer) {}

ReleaseBufferCompletionCallback::~ReleaseBufferCompletionCallback() = default;

void ReleaseBufferCompletionCallback::SetResult(int result) {
  if (!buffer_->HasOneRef())
    result = ERR_FAILED;
  TestCompletionCallback::SetResult(result);
}
}  // namespace net



// -----------------------------------------------------------------------
//this file

using base::Time;
using base::TimeDelta;



// -----------------------------------------------------------------------

namespace disk_cache {
namespace {

    
// -----------------------------------------------------------------------
// TestEntryResult callback define
// Like net::TestCompletionCallback, but for EntryResultCallback.
struct EntryResultIsPendingHelper {
  bool operator()(const disk_cache::EntryResult& result) const {
    return result.net_error() == net::ERR_IO_PENDING;
  }
};
using TestEntryResultCompletionCallbackBase =
    net::internal::TestCompletionCallbackTemplate<disk_cache::EntryResult,
                                                  EntryResultIsPendingHelper>;

class TestEntryResultCompletionCallback
    : public TestEntryResultCompletionCallbackBase {
 public:
  TestEntryResultCompletionCallback();
  ~TestEntryResultCompletionCallback() override;

  disk_cache::Backend::EntryResultCallback callback();

 private:
  DISALLOW_COPY_AND_ASSIGN(TestEntryResultCompletionCallback);
};

TestEntryResultCompletionCallback::TestEntryResultCompletionCallback() =
    default;

TestEntryResultCompletionCallback::~TestEntryResultCompletionCallback() =
    default;

disk_cache::Backend::EntryResultCallback
TestEntryResultCompletionCallback::callback() {
  return base::BindOnce(&TestEntryResultCompletionCallback::SetResult,
                        base::Unretained(this));
}


const char kBlockFileBackendType[] = "block_file";
const char kSimpleBackendType[] = "simple";

const char kDiskCacheType[] = "disk_cache";
const char kAppCacheType[] = "app_cache";

const char kPrivateDirty[] = "Private_Dirty:";
const char kReadWrite[] = "rw-";
const char kHeap[] = "[heap]";
const char kKb[] = "kB";

struct CacheSpec {
 public:
  static std::unique_ptr<CacheSpec> Parse(const std::string& spec_string) {
    std::vector<std::string> tokens = base::SplitString(
        spec_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (tokens.size() != 3)
      return std::unique_ptr<CacheSpec>();
    if (tokens[0] != kBlockFileBackendType && tokens[0] != kSimpleBackendType)
      return std::unique_ptr<CacheSpec>();
    if (tokens[1] != kDiskCacheType && tokens[1] != kAppCacheType)
      return std::unique_ptr<CacheSpec>();

    #if 1
    const base::FilePath::CharType cacheFileName[] =
        L"C:/Users/zhibin/AppData/Roaming/eventdemo/Cache";
    //FilePath log_file_path(kLogFileName);

    return std::unique_ptr<CacheSpec>(new CacheSpec(
        tokens[0] == kBlockFileBackendType ? net::CACHE_BACKEND_BLOCKFILE
                                           : net::CACHE_BACKEND_SIMPLE,
        tokens[1] == kDiskCacheType ? net::DISK_CACHE : net::APP_CACHE,
        base::FilePath(cacheFileName )));
    #else
    return std::unique_ptr<CacheSpec>(new CacheSpec(
        tokens[0] == kBlockFileBackendType ? net::CACHE_BACKEND_BLOCKFILE
                                           : net::CACHE_BACKEND_SIMPLE,
        tokens[1] == kDiskCacheType ? net::DISK_CACHE : net::APP_CACHE,
        base::FilePath(tokens[2])));
#endif
  }

  const net::BackendType backend_type;
  const net::CacheType cache_type;
  const base::FilePath path;

 private:
  CacheSpec(net::BackendType backend_type,
            net::CacheType cache_type,
            const base::FilePath& path)
      : backend_type(backend_type),
        cache_type(cache_type),
        path(path) {
  }
};

void SetSuccessCodeOnCompletion(base::RunLoop* run_loop,
                                bool* succeeded,
                                int net_error) {
  if (net_error == net::OK) {
    *succeeded = true;
  } else {
    *succeeded = false;
  }
  run_loop->Quit();
}



std::unique_ptr<Backend> CreateAndInitBackend(const CacheSpec& spec) {
  std::unique_ptr<Backend> result;
  std::unique_ptr<Backend> backend;
  bool succeeded = false;
  base::RunLoop run_loop;
  net::CompletionOnceCallback callback =
      base::BindOnce(&SetSuccessCodeOnCompletion, &run_loop, &succeeded);
  const int net_error =
      CreateCacheBackend(spec.cache_type, spec.backend_type, spec.path, 0,
                         false, nullptr, &backend, std::move(callback));
  if (net_error == net::OK)
    SetSuccessCodeOnCompletion(&run_loop, &succeeded, net::OK);
  else
    run_loop.Run();
  if (!succeeded) {
    LOG(ERROR) << "Could not initialize backend in "
               << spec.path.LossyDisplayName();
    return result;
  }
  // For the simple cache, the index may not be initialized yet.
  if (spec.backend_type == net::CACHE_BACKEND_SIMPLE) {
    base::RunLoop index_run_loop;
    net::CompletionOnceCallback index_callback = base::BindOnce(
        &SetSuccessCodeOnCompletion, &index_run_loop, &succeeded);
    SimpleBackendImpl* simple_backend =
        static_cast<SimpleBackendImpl*>(backend.get());
    simple_backend->index()->ExecuteWhenReady(std::move(index_callback));
    index_run_loop.Run();
    if (!succeeded) {
      LOG(ERROR) << "Could not initialize Simple Cache in "
                 << spec.path.LossyDisplayName();
      return result;
    }
  }
  DCHECK(backend);
  result.swap(backend);
  return result;
}













#if 1//zhibin:test
int getpid() {
  return 0;
}
#endif

// Parses range lines from /proc/<PID>/smaps, e.g. (anonymous read write):
// 7f819d88b000-7f819d890000 rw-p 00000000 00:00 0
bool ParseRangeLine(const std::string& line,
                    std::vector<std::string>* tokens,
                    bool* is_anonymous_read_write) {
  *tokens = base::SplitString(line, base::kWhitespaceASCII,
                              base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (tokens->size() == 5) {
    const std::string& mode = (*tokens)[1];
    *is_anonymous_read_write = !mode.compare(0, 3, kReadWrite);
    return true;
  }
  // On Android, most of the memory is allocated in the heap, instead of being
  // mapped.
  if (tokens->size() == 6) {
    const std::string& type = (*tokens)[5];
    *is_anonymous_read_write = (type == kHeap);
    return true;
  }
  return false;
}

// Parses range property lines from /proc/<PID>/smaps, e.g.:
// Private_Dirty:        16 kB
//
// Returns |false| iff it recognizes a new range line. Outputs non-zero |size|
// only if parsing succeeded.
bool ParseRangeProperty(const std::string& line,
                        std::vector<std::string>* tokens,
                        uint64_t* size,
                        bool* is_private_dirty) {
  *tokens = base::SplitString(line, base::kWhitespaceASCII,
                              base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // If the line is long, attempt to parse new range outside of this scope.
  if (tokens->size() > 3)
    return false;

  // Skip the line on other parsing error occasions.
  if (tokens->size() < 3)
    return true;
  const std::string& type = (*tokens)[0];
  if (type != kPrivateDirty)
    return true;
  const std::string& unit = (*tokens)[2];
  if (unit != kKb) {
    LOG(WARNING) << "Discarding value not in kB: " << line;
    return true;
  }
  const std::string& size_str = (*tokens)[1];
  uint64_t map_size = 0;
  if (!base::StringToUint64(size_str, &map_size))
    return true;
  *is_private_dirty = true;
  *size = map_size;
  return true;
}

uint64_t GetMemoryConsumption() {
#if 1//zhibin:test
  if (true)
  return 0;
  else {

    #endif
  std::ifstream maps_file(
      base::StringPrintf("/proc/%d/smaps", getpid()).c_str());
  if (!maps_file.good()) {
    LOG(ERROR) << "Could not open smaps file.";
    return false;
  }
  std::string line;
  std::vector<std::string> tokens;
  uint64_t total_size = 0;
  if (!std::getline(maps_file, line) || line.empty())
    return total_size;
  while (true) {
    bool is_anonymous_read_write = false;
    if (!ParseRangeLine(line, &tokens, &is_anonymous_read_write)) {
      LOG(WARNING) << "Parsing smaps - did not expect line: " << line;
    }
    if (!std::getline(maps_file, line) || line.empty())
      return total_size;
    bool is_private_dirty = false;
    uint64_t size = 0;
    while (ParseRangeProperty(line, &tokens, &size, &is_private_dirty)) {
      if (is_anonymous_read_write && is_private_dirty) {
        total_size += size;
        is_private_dirty = false;
      }
      if (!std::getline(maps_file, line) || line.empty())
        return total_size;
    }
  }
  return total_size;
  #if 1//zhibin:test
  }
  #endif
}


// Gets a key's stream to a buffer.
scoped_refptr<net::GrowableIOBuffer> GetStreamForKeyBuffer(
    Backend* backend,
    const std::string& key,
    int index) {

  TestEntryResultCompletionCallback cb_open;
  EntryResult result = backend->OpenEntry(
      key, net::HIGHEST, cb_open.callback());
  result = cb_open.GetResult(std::move(result));
  if (result.net_error() != net::OK) {
    std::cout << "Couldn't find key's entry." << std::endl;
    return nullptr;
  }
  Entry* cache_entry = result.ReleaseEntry();

  const int kInitBufferSize = 8192;
  scoped_refptr<net::GrowableIOBuffer> buffer =
      base::MakeRefCounted<net::GrowableIOBuffer>();
  buffer->SetCapacity(kInitBufferSize);
  net::TestCompletionCallback cb;
  while (true) {
    int rv = cache_entry->ReadData(index, buffer->offset(), buffer.get(),
                                   buffer->capacity() - buffer->offset(),
                                   cb.callback());
    rv = cb.GetResult(rv);
    if (rv < 0) {
      cache_entry->Close();
      std::cout << "Stream read error.." << std::endl;
      return nullptr;
    }
    buffer->set_offset(buffer->offset() + rv);
    if (rv == 0)
      break;
    buffer->SetCapacity(buffer->offset() * 2);
  }
  cache_entry->Close();
  return buffer;
}
using disk_cache::Backend;
using disk_cache::Entry;
using disk_cache::EntryResult;
constexpr int kResponseInfoIndex = 0;
constexpr int kResponseContentIndex = 1;
void GetStreamForKey(Backend* backend, std::string url, int index) {
  std::string key = "https://home.baidu.com/Public/img/play.png?v=12";
  //"https://home.baidu.com/Public/js/fontbase.js?v=12";
 // "http://192.168.50.206:8080/jquery.js";
 // "https://home.baidu.com/Public/img/play.png?v=12";
  // index = 0;//0 header 1,2 
   std::cout << "=== index ="<<index << std::endl;
  scoped_refptr<net::GrowableIOBuffer> buffer(
      GetStreamForKeyBuffer(backend, key, index));
  
  if (index == kResponseInfoIndex) {
    net::HttpResponseInfo response_info;
    bool truncated_response_info = false;
    if (!net::HttpCache::ParseResponseInfo(buffer->StartOfBuffer(),
                                           buffer->offset(), &response_info,
                                           &truncated_response_info)) {
      // This can happen when reading data stored by content::CacheStorage.
      std::cerr << "WARNING: Returning empty response info for key: " << key
                << std::endl;
      //command_marshal->ReturnSuccess();
      //return command_marshal->ReturnString("");
    }
    if (truncated_response_info)
      std::cerr << "WARNING: Truncated HTTP response." << std::endl;
    //command_marshal->ReturnSuccess();
    std::cout<<
        net::HttpUtil::ConvertHeadersBackToHTTPResponse(
            response_info.headers->raw_headers())<<std::endl;
  } else if (index == kResponseContentIndex) {
    //command_marshal->ReturnSuccess();
    std::cout.write(buffer->StartOfBuffer(), buffer->offset());
  }
}


void OnEntryResultComplete(base::RunLoop* run_loop,
                           bool* succeeded,int* sum,
                           EntryResult result) {
  auto rv = result.net_error();

  if (rv == net::OK) {
    *succeeded = true;

    Entry* entry = result.ReleaseEntry();
    std::string url = entry->GetKey();
    (*sum)++;
    std::cout << *sum << "\t:" << url << std::endl;
    entry->Close();

  } else {
    *succeeded = false;
  }

  run_loop->Quit();

  if (rv == net::ERR_FAILED) {
    std::cout << "=== read failed." << std::endl;
  }
}

bool CacheMemTest(const std::vector<std::unique_ptr<CacheSpec>>& specs) {
  std::vector<std::unique_ptr<Backend>> backends;
  for (const auto& it : specs) {
    std::unique_ptr<Backend> backend = CreateAndInitBackend(*it);
    if (!backend) {
      std::cout << "Get backend error.";
      return false;
    }

    std::cout << "Number of entries in " << it->path.LossyDisplayName() << " : "
              << backend->GetEntryCount() << std::endl;
    if (true) {
        //complecated for list
        //http://tb2.bdstatic.com/tb/static-common/img/search_logo_big_v1_8d039f9.png
        //
    if (false) {
      //list
      std::unique_ptr<Backend::Iterator> entry_iterator =
          backend->CreateIterator();
      TestEntryResultCompletionCallback cb;
      EntryResult result = entry_iterator->OpenNextEntry(cb.callback());
      //command_marshal->ReturnSuccess();
      while ((result = cb.GetResult(std::move(result))).net_error() ==
             net::OK) {
        Entry* entry = result.ReleaseEntry();
        std::string url = entry->GetKey();
      
        std ::cout << url << std ::endl;
        entry->Close();
        result = entry_iterator->OpenNextEntry(cb.callback());
      }
        }//command_marshal->ReturnString("");
    else {
      GetStreamForKey(backend.get(), "", 0);
          GetStreamForKey(backend.get(), "", 1);
      GetStreamForKey(backend.get(), "", 2);
    }
    }else {
      bool succeeded0;
      std::unique_ptr<base::RunLoop> run_loop0;
      int sum = 0;

      std::unique_ptr<disk_cache::Backend::Iterator> iter_ =
          backend->CreateIterator();
      while (true) {
        //初始化和重新绑定回调，因为loop只能run一次。
        succeeded0 = false;
        run_loop0 = std::make_unique<base::RunLoop>(
            base::RunLoop::Type::kNestableTasksAllowed);
        disk_cache::Backend::EntryResultCallback entryCallback = base::BindOnce(
            &OnEntryResultComplete, run_loop0.get(), &succeeded0, &sum);

        EntryResult result = iter_->OpenNextEntry(std::move(entryCallback));

        auto rv = result.net_error();
        if (rv != net::OK) {
          // 运行loop.run阻塞等待。。。
          run_loop0->Run();
          run_loop0.reset();         
        } else {
          std::cout << "=== direct get result. net::OK." << std::endl;
          OnEntryResultComplete(run_loop0.get(), &succeeded0, &sum,
                                {});  // std::move(result) EntryResult构造被禁用了。
        }

        if (!succeeded0) {
          std::cout << "Could not get backend in "
                    << it->path.LossyDisplayName();
          break;
        }
      }
    }

    backends.push_back(std::move(backend));
  }
  const uint64_t memory_consumption = GetMemoryConsumption();
  std::cout << "Private dirty memory: " << memory_consumption << " kB"
            << std::endl;
  return true;
}

void PrintUsage(std::ostream* stream) {
  *stream << "Usage: disk_cache_mem_test "
          << "--spec-1=<spec> "
          << "[--spec-2=<spec>]"
          << std::endl
          << "  with <cache_spec>=<backend_type>:<cache_type>:<cache_path>"
          << std::endl
          << "       <backend_type>='block_file'|'simple'" << std::endl
          << "       <cache_type>='disk_cache'|'app_cache'" << std::endl
          << "       <cache_path>=file system path" << std::endl;
}

bool ParseAndStoreSpec(const std::string& spec_str,
                       std::vector<std::unique_ptr<CacheSpec>>* specs) {
  std::unique_ptr<CacheSpec> spec = CacheSpec::Parse(spec_str);
  if (!spec) {
    PrintUsage(&std::cerr);
    return false;
  }
  specs->push_back(std::move(spec));
  return true;
}

bool Main(int argc, char** argv) {
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "disk_cache_memory_test");
  base::CommandLine::Init(argc, argv);
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch("help")) {
    PrintUsage(&std::cout);
    return true;
  }
  if ((command_line.GetSwitches().size() != 1 &&
       command_line.GetSwitches().size() != 2) ||
      !command_line.HasSwitch("spec-1") ||
      (command_line.GetSwitches().size() == 2 &&
       !command_line.HasSwitch("spec-2"))) {
    PrintUsage(&std::cerr);
    return false;
  }
  std::vector<std::unique_ptr<CacheSpec>> specs;
  const std::string spec_str_1 = command_line.GetSwitchValueASCII("spec-1");
  if (!ParseAndStoreSpec(spec_str_1, &specs))
    return false;
  if (command_line.HasSwitch("spec-2")) {
    const std::string spec_str_2 = command_line.GetSwitchValueASCII("spec-2");
    if (!ParseAndStoreSpec(spec_str_2, &specs))
      return false;
  }
  return CacheMemTest(specs);
}

}  // namespace
}  // namespace disk_cache

int main(int argc, char** argv) {
//  int i;
//std::cin >> i;

  return !disk_cache::Main(argc, argv);
}



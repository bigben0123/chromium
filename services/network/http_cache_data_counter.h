// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_HTTP_CACHE_DATA_COUNTER_H_
#define SERVICES_NETWORK_HTTP_CACHE_DATA_COUNTER_H_

#include <memory>
#include <vector>
#include <utility>

#include "base/callback.h"
#include "base/component_export.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"

#include "net/disk_cache/disk_cache.h"
namespace disk_cache {
class Backend;
}

namespace net {
class URLRequestContext;
}

namespace network {

// Helper to count data in HTTP cache.
// Export is for testing only.
class COMPONENT_EXPORT(NETWORK_SERVICE) HttpCacheDataCounter {
 public:
  using HttpCacheDataCounterCallback = base::OnceCallback<
      void(HttpCacheDataCounter*, bool upper_bound, int64_t size_or_error)>;

  // Computes the amount of disk space taken up by entries last used between
  // [start_time, end_time), and return it, or error.  Note that there may be
  // some approximation with respect to both bytes and dates.
  //
  // Furthermore, if there is no efficient way of computing this information,
  // a very loose upper bound (e.g. total disk space used by the cache) may be
  // returned; in that case |upper_bound| will be set to true.
  //
  // Once complete, invokes |callback|, passing |this| and result.
  //
  // If either |this| or |url_request_context| get destroyed, |callback|
  // will not be invoked.
  static std::unique_ptr<HttpCacheDataCounter> CreateAndStart(
      net::URLRequestContext* url_request_context,
      base::Time start_time,
      base::Time end_time,
      HttpCacheDataCounterCallback callback);

#ifndef CUST_NO_FEATURE_CACHE_DATA  // zhibin:
  using GetCacheDataCallback = base::OnceCallback<void(HttpCacheDataCounter*,
                              const std::vector<int8_t>& buffer,
                              int64_t size_or_error)>;

  static std::unique_ptr<HttpCacheDataCounter> CreateAndStart(
      net::URLRequestContext* url_request_context,
      base::Time start_time,
      base::Time end_time,
      const std::string& url,
      GetCacheDataCallback callback);
#endif

  ~HttpCacheDataCounter();

 private:
  HttpCacheDataCounter(base::Time start_time,
                       base::Time end_time,
                       HttpCacheDataCounterCallback callback);

  void GotBackend(std::unique_ptr<disk_cache::Backend*> backend,
                  int error_code);
  void PostResult(bool is_upper_limit, int64_t result_or_error);

  base::WeakPtr<HttpCacheDataCounter> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  base::Time start_time_;
  base::Time end_time_;
  HttpCacheDataCounterCallback callback_;

#ifndef CUST_NO_FEATURE_CACHE_DATA  // zhibin:
  HttpCacheDataCounter(base::Time start_time,
                       base::Time end_time,
                       const std::string& url,
                       GetCacheDataCallback callback);

  // This will trigger the completion callback if appropriate
  void PostResult0();

  GetCacheDataCallback callback0_;
  int index_ = 0;  // 0: http header response; 1: content; 2:js compiled.

  // Tricky here: if |this| gets deleted before |http_cache| gets deleted,
  // GetBackend may still write things out (even though the callback will
  // abort due to weak pointer), so the destination for the pointer can't be
  // owned by |this|.
  //
  // While it can be transferred to the callback to GetBackend, that callback
  // also needs to be kept alive for the duration of this method in order to
  // get at the backend pointer in the synchronous result case.
  // std::unique_ptr<disk_cache::Backend*> backend;
  disk_cache::Backend* backend_;

  scoped_refptr<net::GrowableIOBuffer> iobuffer_ = nullptr;
  std::unique_ptr<std::vector<int8_t>> cacheResult_ =
      std::make_unique<std::vector<int8_t>>();
  disk_cache::EntryResult entryResult_;
  disk_cache::Entry* cache_entry_;
  std::string url_;

  enum Command {
    COMMAND_CACHE_SIZE,
    COMMAND_CACHE_DATA,
    COMMAND_CACHE_LIST,
    COMMAND_CACHE_HEAD,
    COMMAND_CACHE_CONTENT
  };
  Command cmd_;

  enum State {
    STATE_NONE,
    STATE_GET_BACKEND,
    STATE_GET_BACKEND_COMPLETE,
    STATE_OPEN_NEXT_ENTRY,
    STATE_OPEN_NEXT_ENTRY_COMPLETE,
    STATE_OPEN_ENTRY,
    STATE_OPEN_ENTRY_COMPLETE,
    STATE_READ_RESPONSE,
    STATE_READ_RESPONSE_COMPLETE,
    STATE_READ_DATA,
    STATE_READ_DATA_COMPLETE
  };
  State next_state_;

  //Runs the state transition loop.
  int DoLoop(int result);

  // Each of these methods corresponds to a State value. If there is an
  // argument, the value corresponds to the return of the previous state or
  // corresponding callback.
  // int DoGetBackend();
  // int DoGetBackendComplete(int result);
  // int DoOpenNextEntry();
  // int DoOpenNextEntryComplete(int result);
  int DoOpenEntry();
  //  int DoOpenEntryComplete(int result);
  void OpenEntryCallback(disk_cache::EntryResult result);
  int DoReadResponse();
  int DoReadResponseComplete(int result);
  int DoReadData();
  int DoReadDataComplete(int result);
  void HandleResult(int rv); 
  // Called to signal completion of asynchronous IO.
  void OnIOComplete(int result);

  void copy(const char* const p, const int& len);
#endif

  base::WeakPtrFactory<HttpCacheDataCounter> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(HttpCacheDataCounter);
};

}  // namespace network

#endif  // SERVICES_NETWORK_HTTP_CACHE_DATA_COUNTER_H_

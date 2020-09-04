// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/http_cache_data_counter.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/url_request/url_request_context.h"

#include <iostream>

namespace network {

std::unique_ptr<HttpCacheDataCounter> HttpCacheDataCounter::CreateAndStart(
    net::URLRequestContext* url_request_context,
    base::Time start_time,
    base::Time end_time,
    HttpCacheDataCounterCallback callback) {
  HttpCacheDataCounter* instance =
      new HttpCacheDataCounter(start_time, end_time, std::move(callback));
  net::HttpCache* http_cache =
      url_request_context->http_transaction_factory()->GetCache();
  if (!http_cache) {
    // No cache, no space used. Posts a task, so it will run after the return.
    instance->PostResult(false, 0);
  } else {
    std::unique_ptr<disk_cache::Backend*> backend =
        std::make_unique<disk_cache::Backend*>();
    disk_cache::Backend** backend_ptr = backend.get();

    auto get_backend_callback =
        base::BindRepeating(&HttpCacheDataCounter::GotBackend,
                            instance->GetWeakPtr(), base::Passed(&backend));
    int rv = http_cache->GetBackend(backend_ptr, get_backend_callback);
    if (rv != net::ERR_IO_PENDING) {
      instance->GotBackend(std::make_unique<disk_cache::Backend*>(*backend_ptr),
                           rv);
    }
  }

  return base::WrapUnique(instance);
}

std::unique_ptr<HttpCacheDataCounter> HttpCacheDataCounter::CreateAndStart(
    net::URLRequestContext* url_request_context,
    base::Time start_time,
    base::Time end_time,
    const std::string& url,
    HttpCacheDataCounterCallback0 callback) {
  HttpCacheDataCounter* instance =
      new HttpCacheDataCounter(start_time, end_time, url,std::move(callback));
  net::HttpCache* http_cache =
      url_request_context->http_transaction_factory()->GetCache();
  if (!http_cache) {
    // No cache, no space used. Posts a task, so it will run after the return.
    instance->PostResult(false, 0);
  } else {
    std::unique_ptr<disk_cache::Backend*> backend =
        std::make_unique<disk_cache::Backend*>();
    disk_cache::Backend** backend_ptr = backend.get();

    auto get_backend_callback =
        base::BindRepeating(&HttpCacheDataCounter::GotBackend,
                            instance->GetWeakPtr(), base::Passed(&backend));
    int rv = http_cache->GetBackend(backend_ptr, get_backend_callback);
    if (rv != net::ERR_IO_PENDING) {
      instance->GotBackend(std::make_unique<disk_cache::Backend*>(*backend_ptr),
                           rv);
    }
  }

  return base::WrapUnique(instance);
}

HttpCacheDataCounter::HttpCacheDataCounter(
    base::Time start_time,
    base::Time end_time,
    HttpCacheDataCounterCallback callback)
    : index_(0),
      next_state_(STATE_NONE),
      start_time_(start_time),
      end_time_(end_time),
      callback_(std::move(callback)),
      cmd_(COMMAND_CACHE_SIZE) {
  std::cout << "XXXXXXXXXXXXXXXXXXXXXXXXXXXX  careate" << std::endl;
}

HttpCacheDataCounter::HttpCacheDataCounter(
    base::Time start_time,
    base::Time end_time,
    const std::string& url,
    HttpCacheDataCounterCallback0 callback)
    : index_(0),
      next_state_(STATE_NONE),
      start_time_(start_time),
      end_time_(end_time),
      callback0_(std::move(callback)),
      url_(url),
      cmd_(COMMAND_CACHE_DATA) {
  std::cout << "XXXXXXXXXXXXXXXXXXXXXXXXXXXX  careate" << std::endl;
}

HttpCacheDataCounter::~HttpCacheDataCounter() {
  std::cout << "XXXXXXXXXXXXXXXXXXXXXXXXXXXX   You got to kill me" << std::endl;
}

void HttpCacheDataCounter::GotBackend(
    std::unique_ptr<disk_cache::Backend*> backend,
    int error_code) {
  DCHECK_LE(error_code, 0);

  if (cmd_ == COMMAND_CACHE_SIZE) {
    bool is_upper_limit = false;
    if (error_code != net::OK) {
      PostResult(is_upper_limit, error_code);
      return;
    }

    if (!*backend) {
      PostResult(is_upper_limit, 0);
      return;
    }

    int64_t rv;
    backend_ = *backend;
    disk_cache::Backend* cache = *backend;

    // Handle this here since some backends would DCHECK on this.
    if (start_time_ > end_time_) {
      PostResult(is_upper_limit, 0);
      return;
    }

    if (start_time_.is_null() && end_time_.is_max()) {
      rv = cache->CalculateSizeOfAllEntries(base::BindOnce(
          &HttpCacheDataCounter::PostResult, GetWeakPtr(), is_upper_limit));
    } else {
      rv = cache->CalculateSizeOfEntriesBetween(
          start_time_, end_time_,
          base::BindOnce(&HttpCacheDataCounter::PostResult, GetWeakPtr(),
                         is_upper_limit));
      if (rv == net::ERR_NOT_IMPLEMENTED) {
        is_upper_limit = true;
        rv = cache->CalculateSizeOfAllEntries(base::BindOnce(
            &HttpCacheDataCounter::PostResult, GetWeakPtr(), is_upper_limit));
      }
    }
    if (rv != net::ERR_IO_PENDING)
      PostResult(is_upper_limit, rv);
  
  } else if (cmd_ == COMMAND_CACHE_DATA) {  
         
    if (error_code != net::OK) {
      PostResult0();
      return;
    }

    if (!*backend) {
      PostResult0();
      return;
    }
    backend_ = *backend;

    // Handle this here since some backends would DCHECK on this.
    if (start_time_ > end_time_) {
      PostResult0();
      return;
    }

    if (start_time_.is_null() && end_time_.is_max()) {
      next_state_ = STATE_OPEN_ENTRY;
      DoLoop(net::OK);
    } else {
      //todo:
    } 
  } else if (cmd_ == COMMAND_CACHE_LIST) {
      //todo:
  }
 
}

void HttpCacheDataCounter::PostResult(bool is_upper_limit,
                                      int64_t result_or_error) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback_), this, is_upper_limit,
                                result_or_error));
}

int HttpCacheDataCounter::DoLoop(int result) {
  DCHECK(next_state_ != STATE_NONE);
  std::cout << "========  DoLoop begin result=" << result << std::endl;
  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_GET_BACKEND:
        // DCHECK_EQ(net::OK, rv);
        // rv = DoGetBackend();
        break;
      case STATE_GET_BACKEND_COMPLETE:
        // rv = DoGetBackendComplete(rv);
        break;
      case STATE_OPEN_NEXT_ENTRY:
        // DCHECK_EQ(net::OK, rv);
        // rv = DoOpenNextEntry();
        break;
      case STATE_OPEN_NEXT_ENTRY_COMPLETE:
        // rv = DoOpenNextEntryComplete(rv);
        break;
      case STATE_OPEN_ENTRY:
        DCHECK_EQ(net::OK, rv);
        rv = DoOpenEntry();
        break;
      case STATE_OPEN_ENTRY_COMPLETE:
        // rv = DoOpenEntryComplete(rv);
        break;
      case STATE_READ_RESPONSE:
        DCHECK_EQ(net::OK, rv);
        rv = DoReadResponse();
        break;
      case STATE_READ_RESPONSE_COMPLETE:
        rv = DoReadResponseComplete(rv);
        break;
      case STATE_READ_DATA:
        DCHECK_EQ(net::OK, rv);
        rv = DoReadData();
        break;
      case STATE_READ_DATA_COMPLETE:
        rv = DoReadDataComplete(rv);
        break;

      default:
        NOTREACHED() << "bad state";
        rv = net::ERR_FAILED;
        break;
    }
  } while (rv != net::ERR_IO_PENDING && next_state_ != STATE_NONE);

  if (rv != net::ERR_IO_PENDING)
    HandleResult(rv);
  std::cout << "========DoLoop end  " << std::endl;
  return rv;
}

int HttpCacheDataCounter::DoOpenEntry() {
  std::cout << "========    DoOpenEntry" << std::endl;
  entryResult_ = backend_->OpenEntry(
      url_, net::HIGHEST,
      base::BindOnce(&HttpCacheDataCounter::OpenEntryCallback, GetWeakPtr()));
  return net::ERR_IO_PENDING;
}

void HttpCacheDataCounter::OpenEntryCallback(disk_cache::EntryResult result) {
  next_state_ = STATE_READ_RESPONSE;
  std::cout << "=== OpenEntryCallback " << std::endl;
  cache_entry_ = result.ReleaseEntry();
  std::cout << "=== OpenEntryCallback " << cache_entry_ << std::endl;
  if (!cache_entry_)
    PostResult0();
  else
    DoLoop(net::OK);
}

int HttpCacheDataCounter::DoReadResponse() {
  std::cout << "========  STATE_READ_RESPONSE  begin" << std::endl;
  next_state_ = STATE_READ_RESPONSE_COMPLETE;

  if (iobuffer_ == nullptr) {
    // init
    const int kInitBufferSize = cache_entry_->GetDataSize(0);
    if (kInitBufferSize < 1) {
      NOTREACHED() << "cache_entry_->GetDataSize(0) return less than 1.";
      return net::ERR_FAILED;
    }
    iobuffer_ = base::MakeRefCounted<net::GrowableIOBuffer>();
    iobuffer_->SetCapacity(kInitBufferSize);  // size of header
  }

  return cache_entry_->ReadData(
      index_, iobuffer_->offset(), iobuffer_.get(),
      iobuffer_->capacity() - iobuffer_->offset(),
      base::BindRepeating(&HttpCacheDataCounter::OnIOComplete, GetWeakPtr()));
}

int HttpCacheDataCounter::DoReadResponseComplete(int result) {
  if (result > 0) {
    iobuffer_->set_offset(iobuffer_->offset() + result);
  }
  //http response must be returned in once call.
  if (result && result == cache_entry_->GetDataSize(index_)) {
    net::HttpResponseInfo response_info;
    bool truncated_response_info = false;
    if (!net::HttpCache::ParseResponseInfo(iobuffer_->StartOfBuffer(),
                                           iobuffer_->offset(), &response_info,
                                           &truncated_response_info)) {
      // This can happen when reading data stored by content::CacheStorage.
      std::cerr << "WARNING: Returning empty response info for key: "
                << std::endl;
      return net::ERR_FAILED;
    }
    if (truncated_response_info) {
      std::cerr << "WARNING: Truncated HTTP response." << std::endl;
      return net::ERR_FAILED;
    } 
    auto resp = net::HttpUtil::ConvertHeadersBackToHTTPResponse(
        response_info.headers->raw_headers());
   
    copy(resp.data(), resp.length());
    //std::cout << resp << std::endl;
  }  
  
  //enter next loop directly.
  index_ = 1;//read content
  next_state_ = STATE_READ_DATA;
  return net::OK;
}

int HttpCacheDataCounter::DoReadData() {
  next_state_ = STATE_READ_DATA_COMPLETE;
  int buf_len_ = cache_entry_->GetDataSize(index_);
  if (!buf_len_)
    return buf_len_;

  iobuffer_->SetCapacity(buf_len_);//iobuffer_->capacity() + 
  iobuffer_->set_offset(0);  
  return cache_entry_->ReadData(
      index_, iobuffer_->offset(), iobuffer_.get(),
      iobuffer_->capacity() - iobuffer_->offset(),
      base::BindRepeating(&HttpCacheDataCounter::OnIOComplete, GetWeakPtr()));
}

int HttpCacheDataCounter::DoReadDataComplete(int result) {
  std::cout << "========       DoReadDataComplete  len=" << result << std::endl;
  if (result > 0) {
    iobuffer_->set_offset(iobuffer_->offset() + result);
  }

  if (iobuffer_->capacity() <= iobuffer_->offset()) {
    // succ
    // std::cout.write(iobuffer_->StartOfBuffer(), iobuffer_->offset());
    std::cout << "========    XXX  cache_entry_->Close()" << std::endl;
    cache_entry_->Close();
    cache_entry_ = NULL;
    return net::OK;
  }
  return net::ERR_IO_PENDING;
}

void HttpCacheDataCounter::OnIOComplete(int rv) {
  std::cout << "========       OnIOComplete  len=" << rv << std::endl;
  DoLoop(rv);
}

#if 0
int ViewCacheHelper::DoReadResponseComplete(int result) {
  HandleResult(rv);

  index_ = 0;
  next_state_ = STATE_NONE;
  return OK;
}

int HttpCacheDataCounter::DoOpenEntryComplete(int result) {
  std::cout << "========  DoOpenEntryComplete" << std::endl;

  return OK;
}
#endif

void HttpCacheDataCounter::HandleResult(int rv) {
  DCHECK_NE(net::ERR_IO_PENDING, rv);
  // DCHECK_NE(net::ERR_FAILED, rv);
  std::cout << "========  HandleResult" << std::endl;
  if (rv == net::ERR_FAILED) {
    std::cout << "========    XXX  cache_entry_->Close()" << std::endl;
    if (cache_entry_)
      cache_entry_->Close();
    std::cout << "Stream read error.." << std::endl;
    return;
  }else  if (rv == net::OK) {  // suc
    copy(iobuffer_->StartOfBuffer(), iobuffer_->offset());
  }

  PostResult0();
}

void HttpCacheDataCounter::PostResult0() {

  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback0_), this, *(cacheResult_.get()),
                                cacheResult_->size()));
}

void HttpCacheDataCounter::copy(const char* const p, const int& len) {
  std::cout << "========  copy len=" << len << std::endl;
  int8_t* initPtr = (int8_t*)(p);
  int8_t* ptr = (int8_t*)(p);
  while (ptr < initPtr + len) {
    cacheResult_->push_back(*ptr);
    ptr++;
  }
}

}  // namespace network

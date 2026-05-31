// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#ifndef DATA_STRUCT_H
#define DATA_STRUCT_H

#include<inttypes.h>
#include<optional>
#include<memory>
#include<functional>
#include<map>
#include<liburing.h>

namespace mAsyncDiskIO{
    class async_result;

    using unique_buf=std::unique_ptr<uint8_t[],std::function<void(uint8_t*)>>;
    inline unique_buf make_unique_buf(uint8_t* ptr=nullptr,std::function<void(uint8_t*)> fn=[](uint8_t* ptr){if(ptr) delete[] ptr;}){
        return unique_buf{ptr,fn};
    };

    enum class RW{
        READ,
        WRITE
    };

    struct use_data
    {
        uint64_t id=0;
        uint64_t use_d=0;//用户数据，用于标识，如果是指针则其生命周期由外界保证
        unique_buf buf;
        RW rw=RW::READ;
    };

    using user_data_map=std::map<uint64_t,use_data>;
    using shared_user_data_map=std::shared_ptr<user_data_map>;
    using weak_user_data_map=std::weak_ptr<user_data_map>;

    using unique_result=std::unique_ptr<async_result>;

    using shared_uring=std::shared_ptr<io_uring>;
    using weak_uring=std::weak_ptr<io_uring>;

    enum class state{
        ERROR,
        FINISH,
        UNFINISHED
    };
};

#include"result.h"

#endif //DATA_STRUCT_H
// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#ifndef RESULT_READ_H
#define RESULT_READ_H

#include<liburing.h>
#include"data_struct.h"

namespace mAsyncDiskIO{

    class async_result{
        private:
        weak_uring ring;
        weak_user_data_map map;
        use_data us_d;//用户数据
        bool is_valid=false;
        int byte_size=0;

        void finish(io_uring_cqe* cqe);

        public:
        explicit async_result(weak_uring ring,weak_user_data_map map);
        ~async_result()=default;
        async_result(async_result&)=delete;
        async_result(async_result&&) noexcept;
        async_result& operator=(async_result&)=delete;
        async_result& operator=(async_result&&) noexcept;

        RW rw();
        bool valid();
        bool buf_empty();
        state peek();
        int wait();
        uint64_t user_data();
        unique_buf transfer_data();
        int size();
    };

};

#endif //RESULT_READ_H
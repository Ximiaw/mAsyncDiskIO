// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#ifndef RESULT_READ_H
#define RESULT_READ_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_read:public async_result_base{
        private:
        friend class async_io;

        weak_uring ring;
        io_uring_cqe* cqe=nullptr;
        use_data* use_d=nullptr;

        public:
        explicit async_result_read(weak_uring ring);
        ~async_result_read();
        async_result_read(async_result_read&)=delete;
        async_result_read(async_result_read&&) noexcept;
        async_result_read& operator=(async_result_read&)=delete;
        async_result_read& operator=(async_result_read&&) noexcept;

        bool empty();
        state peek();
        int wait();
        optional_ui64 user_data();
        unique_buf transfer_data();
        size_t size();
        void finish();
    };

};

#endif //RESULT_READ_H
// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#ifndef RESULT_WRITE_H
#define RESULT_WRITE_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_write:public async_result_base{
        private:
        friend class async_io;

        weak_uring ring;
        io_uring_cqe* cqe=nullptr;
        use_data* use_d=nullptr;

        public:
        explicit async_result_write(weak_uring ring);
        ~async_result_write();
        async_result_write(async_result_write&)=delete;
        async_result_write(async_result_write&&) noexcept;
        async_result_write& operator=(async_result_write&)=delete;
        async_result_write& operator=(async_result_write&&) noexcept;

        bool empty();
        state peek();
        int wait();
        optional_ui64 user_data();
        size_t size();
        void finish();
    };

};

#endif //RESULT_WRITE_H
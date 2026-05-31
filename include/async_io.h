// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include<liburing.h>
#include"data_struct.h"

namespace mAsyncDiskIO{

    class async_io{
        private:
        uint64_t id=0;
        shared_uring ring;
        shared_user_data_map map;//结果对象在读到cqe后从map里面移走所读数据
        size_t count_result=0;
        bool submit_error=false;

        public:
        async_io(size_t deep=32,unsigned flags=IORING_SETUP_SQPOLL);
        ~async_io();
        async_io(async_io&)=delete;
        async_io(async_io&&)=delete;
        async_io& operator=(async_io&)=delete;
        async_io& operator=(async_io&&)=delete;

        public:
        unique_result read(int fd,uint32_t size,uint64_t user_data,uint64_t offset=0);
        bool prep_read(int fd,uint32_t size,uint64_t user_data,uint64_t offset=0);

        unique_result write(int fd,unique_buf&& buf,uint32_t size,uint64_t user_data,uint64_t offset=0);
        bool prep_write(int fd,unique_buf&& buf,uint32_t size,uint64_t user_data,uint64_t offset=0);

        size_t result_count();
        unique_result get_result();

        size_t queue_size();

        bool submit();
        bool submit_err();
    };

};

#endif //ASYNC_IO_H
#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include<liburing.h>
#include"data_struct.h"
#include"result_read.h"
#include"result_write.h"

namespace mAsyncDiskIO{

    class async_io{
        private:
        shared_uring ring;

        public:
        async_io(size_t deep=32);
        ~async_io();
        async_io(async_io&)=delete;
        async_io(async_io&&)=delete;
        async_io& operator=(async_io&)=delete;
        async_io& operator=(async_io&&)=delete;

        public:
        shared_result_read read(int fd,size_t size,uint64_t offset,uint64_t user_data);
        shared_result_write write(int fd,unique_buf&& buf,size_t size,uint64_t offset,uint64_t user_data);
    };

};

#endif //ASYNC_IO_H
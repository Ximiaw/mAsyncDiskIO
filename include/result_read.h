#ifndef RESULT_READ_H
#define RESULT_READ_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_read:public async_result_base{
        private:
        friend class async_io;

        io_uring* ring;
        io_uring_cqe* cqe=nullptr;
        weak_result weak_r;
        result_set* set;

        public:
        explicit async_result_read(io_uring* ring,result_set* set);
        ~async_result_read();
        async_result_read(async_result_read&)=delete;
        async_result_read(async_result_read&&) noexcept;
        async_result_read& operator=(async_result_read&)=delete;
        async_result_read& operator=(async_result_read&&) noexcept;

        bool empty();
        bool peek();
        size_t wait();
        uint64_t user_data();
        unique_buf transfer_data();
        size_t size();
        void finish();
    };

};

#endif //RESULT_READ_H
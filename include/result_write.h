#ifndef RESULT_WRITE_H
#define RESULT_WRITE_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_write:async_result_base{
        private:
        friend class async_io;

        io_uring_cqe* cqe;
        weak_result weak_r;
        result_set* set;

        public:
        explicit async_result_write(io_uring_cqe* cqe,result_set* set,shared_result* sr);
        ~async_result_write();
        async_result_write(async_result_write&)=delete;
        async_result_write(async_result_write&&) noexcept;
        async_result_write& operator=(async_result_write&)=delete;
        async_result_write& operator=(async_result_write&&) noexcept;

        bool empty();
        bool peek();
        size_t wait();
        uint64_t user_data();
        size_t size();
        void finish();
    };

};

#endif //RESULT_WRITE_H
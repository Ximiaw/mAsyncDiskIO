#ifndef RESULT_READ_H
#define RESULT_READ_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_read:async_result_base{
        private:
        friend class async_io;

        io_uring_cqe* cqe;
        use_data use_d;
        weak_result weak_r;
        result_set* set;

        public:
        explicit async_result_read(io_uring_cqe* cqe,size_t buf_size,result_set* set,shared_result* sr);
        ~async_result_read();
        async_result_read(async_result_read&)=delete;
        async_result_read(async_result_read&&) noexcept;
        async_result_read& operator=(async_result_read&)=delete;
        async_result_read& operator=(async_result_read&&) noexcept;

        bool empty();
        bool peek();
        size_t wait();
        uint64_t user_data();
        uint8_t* data();//不允许在外部delete
        size_t size();
        void finish();
    };

};

#endif //RESULT_READ_H
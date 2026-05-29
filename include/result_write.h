#ifndef RESULT_WRITE_H
#define RESULT_WRITE_H

#include<liburing.h>
#include"data_struct.h"
#include"result_base.h"

namespace mAsyncDiskIO{

    class async_result_write:public async_result_base{
        private:
        friend class async_io;

        io_uring* ring;
        io_uring_cqe* cqe=nullptr;
        weak_result_write weak_r;
        result_set* set;

        public:
        explicit async_result_write(io_uring* ring,result_set* set);
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
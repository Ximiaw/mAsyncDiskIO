#include"result_read.h"

namespace mAsyncDiskIO{

    async_result_read::async_result_read(weak_uring ring):ring(ring){};

    async_result_read::~async_result_read(){
        finish();
        if(use_d) delete use_d;
    };

    async_result_read::async_result_read(async_result_read&& other) noexcept{
        ring=std::move(other.ring);
        cqe=other.cqe;

        other.cqe=nullptr;
    };

    async_result_read& async_result_read::operator=(async_result_read&& other) noexcept{
        if(this==&other) return *this;
        ring=std::move(other.ring);
        cqe=other.cqe;

        other.cqe=nullptr;
        return *this;
    };

    bool async_result_read::empty(){
        return use_d==nullptr;
    };

    state async_result_read::peek(){
        if(ring.expired()) return state::ERROR;
        if(cqe) return state::FINISH;
        int ret = io_uring_peek_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return state::UNFINISHED;
        use_d=reinterpret_cast<use_data*>(cqe->user_data);
        return state::FINISH;
    }

    int async_result_read::wait(){
        if(ring.expired()) return -1;
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return -1;
        use_d=reinterpret_cast<use_data*>(cqe->user_data);
        return cqe->res;
    };

    optional_ui64 async_result_read::user_data(){
        if(!use_d) return optional_ui64{};
        return optional_ui64{use_d->use_d};
    };

    unique_buf async_result_read::transfer_data(){
        if(!use_d) return make_unique_buf();
        uint8_t* ptr = use_d->buf;
        use_d->buf=nullptr;
        return make_unique_buf(ptr);
    };

    size_t async_result_read::size(){
        if(!cqe) return 0;
        return cqe->res;
    };

    void async_result_read::finish(){
        if(ring.expired()) return;
        if(!cqe&&wait()==-1) return;
        io_uring_cqe_seen(ring.lock().get(),cqe);
        ring.reset();
        cqe=nullptr;
    };
};
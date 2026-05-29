#include"result_write.h"

namespace mAsyncDiskIO{

    async_result_write::async_result_write(weak_uring ring):ring(ring){};

    async_result_write::~async_result_write(){
        finish();
        if(use_d) delete use_d;
    };

    async_result_write::async_result_write(async_result_write&& other) noexcept{
        ring=std::move(other.ring);
        cqe=other.cqe;
        use_d=other.use_d;

        other.use_d=nullptr;
        other.cqe=nullptr;
    };

    async_result_write& async_result_write::operator=(async_result_write&& other) noexcept{
        if(this==&other) return *this;
        ring=std::move(other.ring);
        cqe=other.cqe;
        use_d=other.use_d;

        other.use_d=nullptr;
        other.cqe=nullptr;
        return *this;
    };

    bool async_result_write::empty(){
        return use_d==nullptr;
    };

    state async_result_write::peek(){
        if(ring.expired()) return state::ERROR;
        if(cqe) return state::FINISH;
        int ret = io_uring_peek_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return state::UNFINISHED;
        use_d = reinterpret_cast<use_data*>(cqe->user_data);
        delete[] use_d->buf;
        use_d->buf=nullptr;
        return state::FINISH;
    };

    int async_result_write::wait(){
        if(ring.expired()) return -1;
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return -1;
        use_d = reinterpret_cast<use_data*>(cqe->user_data);
        delete[] use_d->buf;
        use_d->buf=nullptr;
        return cqe->res;
    };

    optional_ui64 async_result_write::user_data(){
        if(!use_d) return optional_ui64{};
        return optional_ui64{use_d->use_d};
    };

    size_t async_result_write::size(){
        if(!cqe) return 0;
        return cqe->res;
    };

    void async_result_write::finish(){
        if(ring.expired()) return;
        if(!cqe&&wait()==-1) return;
        io_uring_cqe_seen(ring.lock().get(),cqe);
        ring.reset();
        cqe=nullptr;
    };

};
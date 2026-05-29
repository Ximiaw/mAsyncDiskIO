#include"result_write.h"

namespace mAsyncDiskIO{

    async_result_write::async_result_write(io_uring* ring):ring(ring){};

    async_result_write::~async_result_write(){
        if(!ring) return;
        finish();
    };

    async_result_write::async_result_write(async_result_write&& other) noexcept{
        ring=other.ring;
        cqe=other.cqe;

        other.ring=nullptr;
        other.cqe=nullptr;
    };

    async_result_write& async_result_write::operator=(async_result_write&& other) noexcept{
        if(this==&other) return *this;
        ring=other.ring;
        cqe=other.cqe;

        other.ring=nullptr;
        other.cqe=nullptr;
        return *this;
    };

    bool async_result_write::empty(){
        return ring==nullptr||cqe==nullptr;
    };

    state async_result_write::peek(){
        if(cqe) return state::FINISH;
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret!=0) return state::UNFINISHED;
        use_data* ud = reinterpret_cast<use_data*>(cqe->user_data);
        delete[] ud->buf;
        ud->buf=nullptr;
        return state::FINISH;
    };

    int async_result_write::wait(){
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0) return -1;
        use_data* ud = reinterpret_cast<use_data*>(cqe->user_data);
        delete[] ud->buf;
        ud->buf=nullptr;
        return cqe->res;
    };

    optional_ui64 async_result_write::user_data(){
        if(!cqe) return optional_ui64{};
        return optional_ui64{reinterpret_cast<use_data*>(cqe->user_data)->ues_d};
    };

    size_t async_result_write::size(){
        if(!cqe) return 0;
        return cqe->res;
    };

    void async_result_write::finish(){
        if(!ring) return;
        if(!cqe) wait();
        delete reinterpret_cast<use_data*>(cqe->user_data);
        cqe->user_data=0;
        io_uring_cqe_seen(ring,cqe);
        ring=nullptr;
        cqe=nullptr;
    };

};
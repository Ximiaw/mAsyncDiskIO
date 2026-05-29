#include"result_read.h"

namespace mAsyncDiskIO{

    async_result_read::async_result_read(io_uring* ring):ring(ring){};

    async_result_read::~async_result_read(){
        if(!ring) return;
        finish();
    };

    async_result_read::async_result_read(async_result_read&& other) noexcept{
        ring=other.ring;
        cqe=other.cqe;

        other.ring=nullptr;
        other.cqe=nullptr;
    };

    async_result_read& async_result_read::operator=(async_result_read&& other) noexcept{
        if(this==&other) return *this;
        ring=other.ring;
        cqe=other.cqe;

        other.ring=nullptr;
        other.cqe=nullptr;
        return *this;
    };

    bool async_result_read::empty(){
        return ring==nullptr||cqe==nullptr;
    };

    state async_result_read::peek(){
        if(cqe) return state::FINISH;
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret!=0) return state::UNFINISHED;
        return state::FINISH;
    }

    int async_result_read::wait(){
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0) return -1;
        return cqe->res;
    };

    optional_ui64 async_result_read::user_data(){
        if(!cqe) return optional_ui64{};
        return optional_ui64{reinterpret_cast<use_data*>(cqe->user_data)->ues_d};
    };

    unique_buf async_result_read::transfer_data(){
        if(!cqe) return make_unique_buf();
        uint8_t* ptr = reinterpret_cast<use_data*>(cqe->user_data)->buf;
        reinterpret_cast<use_data*>(cqe->user_data)->buf=nullptr;
        return make_unique_buf(ptr);
    };

    size_t async_result_read::size(){
        if(!cqe) return 0;
        return cqe->res;
    };

    void async_result_read::finish(){
        if(!ring) return;
        if(!cqe) wait();
        delete reinterpret_cast<use_data*>(cqe->user_data);
        cqe->user_data=0;
        io_uring_cqe_seen(ring,cqe);
        ring=nullptr;
        cqe=nullptr;
    };
};
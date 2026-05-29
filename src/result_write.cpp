#include"result_write.h"

namespace mAsyncDiskIO{

    async_result_write::async_result_write(io_uring* ring,result_set* set):ring(ring),set(set){};

    async_result_write::~async_result_write(){
        if(!ring) return;
        finish();
    };

    async_result_write::async_result_write(async_result_write&& other) noexcept{
        ring=other.ring;
        set=other.set;
        cqe=other.cqe;
        weak_r=other.weak_r;

        other.ring=nullptr;
        other.set=nullptr;
        other.cqe=nullptr;
        other.weak_r=weak_result_write{};
    };

    async_result_write& async_result_write::operator=(async_result_write&& other) noexcept{
        if(this==&other) return *this;
        ring=other.ring;
        set=other.set;
        cqe=other.cqe;
        weak_r=other.weak_r;

        other.ring=nullptr;
        other.set=nullptr;
        other.cqe=nullptr;
        other.weak_r=weak_result_write{};
        return *this;
    };

    bool async_result_write::empty(){
        return ring==nullptr||cqe==nullptr;
    };

    bool async_result_write::peek(){
        if(cqe) return true;
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret!=0) return false;
        return true;
    };

    size_t async_result_write::wait(){
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0) 0;
        return cqe->res;
    };

};
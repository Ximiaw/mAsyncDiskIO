#include"result_read.h"

namespace mAsyncDiskIO{

    async_result_read::async_result_read(io_uring* ring,result_set* set):ring(ring),set(set){};

    async_result_read::~async_result_read(){
        if(!ring) return;
        finish();
    };

    async_result_read::async_result_read(async_result_read&& other) noexcept{
        ring=other.ring;
        set=other.set;
        cqe=other.cqe;
        weak_r=other.weak_r;

        other.ring=nullptr;
        other.set=nullptr;
        other.cqe=nullptr;
        other.weak_r=weak_result{};
    };

    async_result_read& async_result_read::operator=(async_result_read&& other) noexcept{
        if(this==&other) return *this;
        ring=other.ring;
        set=other.set;
        cqe=other.cqe;
        weak_r=other.weak_r;

        other.ring=nullptr;
        other.set=nullptr;
        other.cqe=nullptr;
        other.weak_r=weak_result{};
        return *this;
    };

    bool async_result_read::empty(){
        return ring==nullptr||cqe==nullptr;
    };

    bool async_result_read::peek(){
        if(cqe) return true;
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret!=0) return false;
        return true;
    }

    size_t async_result_read::wait(){
        if(cqe) return cqe->res;
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0) return 0;
        return cqe->res;
    };

    uint64_t async_result_read::user_data(){
        if(!cqe) return 0;
        return reinterpret_cast<use_data*>(cqe->user_data)->ues_d;
    };

    uint8_t* async_result_read::data(){
        if(!cqe) return nullptr;
        return reinterpret_cast<use_data*>(cqe->user_data)->buf;
    };

    size_t async_result_read::size(){
        if(!cqe) return 0;
        return cqe->res;
    };

    void async_result_read::finish(){
        set->erase(weak_r.lock());
        if(!cqe){
            ring=nullptr;
            return;
        };
        delete reinterpret_cast<use_data*>(cqe->user_data);
        io_uring_cqe_seen(ring,cqe);
        ring=nullptr;
        cqe=nullptr;
        set=nullptr;
        weak_r=weak_result{};
    };
};
// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#include"result.h"

namespace mAsyncDiskIO{

    async_result::async_result(weak_uring ring,weak_user_data_map map):ring(ring),map(map){
        is_valid=!ring.expired()&&!map.expired();
    };

    async_result::async_result(async_result&& other) noexcept{
        ring=std::move(other.ring);
        map=std::move(other.map);
        us_d=std::move(other.us_d);
        byte_size=std::move(other.byte_size);
        is_valid=std::move(other.is_valid);
        other.is_valid=false;
    };

    async_result& async_result::operator=(async_result&& other) noexcept{
        if(this==&other) return *this;
        ring=std::move(other.ring);
        map=std::move(other.map);
        us_d=std::move(other.us_d);
        byte_size=std::move(other.byte_size);
        is_valid=std::move(other.is_valid);
        other.is_valid=false;
        return *this;
    };

    RW async_result::rw(){
        return us_d.rw;
    };

    bool async_result::valid(){
        return is_valid&&!ring.expired()&&!map.expired();
    };

    bool async_result::buf_empty(){
        return !us_d.buf.operator bool();
    };

    state async_result::peek(){
        if(!buf_empty()) return state::FINISH;
        if(!valid()) return state::ERROR;
        io_uring_cqe* cqe=nullptr;
        int ret = io_uring_peek_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return state::UNFINISHED;
        finish(cqe);
        return state::FINISH;
    };

    int async_result::wait(){
        if(!buf_empty()) return byte_size;
        if(!valid()) return -1;
        io_uring_cqe* cqe=nullptr;
        int ret = io_uring_wait_cqe(ring.lock().get(),&cqe);
        if(ret!=0) return -1;
        finish(cqe);
        return byte_size;
    };

    uint64_t async_result::user_data(){
        return us_d.use_d;
    };

    unique_buf async_result::transfer_data(){
        if(buf_empty()) return make_unique_buf();
        if(rw()==RW::WRITE) return make_unique_buf();
        is_valid=false;
        return std::move(us_d.buf);
    };

    int async_result::size(){
        return byte_size;
    };

    void async_result::finish(io_uring_cqe* cqe){
        shared_user_data_map sudm = map.lock();
        us_d=std::move(sudm->at(cqe->user_data));
        sudm->erase(cqe->user_data);
        byte_size=cqe->res;
        io_uring_cqe_seen(ring.lock().get(),cqe);
    };

};
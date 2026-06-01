// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#include"async_io.h"

namespace mAsyncDiskIO{

    async_io::async_io(size_t deep,unsigned flags):ring(new io_uring),map(new user_data_map){
        int ret = io_uring_queue_init(deep,ring.get(),flags);
        if(ret!=0) throw std::runtime_error("async_io:init error");
    };

    async_io::~async_io(){
        if(ring) io_uring_queue_exit(ring.get());
    };

    unique_result async_io::read(int fd,unique_buf&& buf,uint32_t size,uint64_t user_data,uint64_t offset){
        submit_error=false;
        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return nullptr;
        
        uint64_t key = id++;
        use_data ud{key,user_data,std::move(buf),RW::READ};
        uint8_t* buf_p = ud.buf.get();

        map->insert({key,std::move(ud)});
        io_uring_prep_read(sqe,fd,buf_p,size,offset);
        sqe->user_data=key;//手动处理的，不转成指针，通过map查询可以使得缓冲区受到map管理

        if(io_uring_submit(ring.get())<0){
            count_result++;//提交失败，但sqe已经准备，可能随着后续某一次调用submit提交
            submit_error=true;
            return nullptr;
        }
        return std::make_unique<async_result>(ring,map);
    };

    bool async_io::prep_read(int fd,uint32_t size,uint64_t user_data,uint64_t offset){
        unique_buf buf = make_unique_buf(new uint8_t[size]);

        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return false;

        uint64_t key = id++;
        use_data ud{key,user_data,std::move(buf),RW::READ};
        uint8_t* buf_p = ud.buf.get();

        map->insert({key,std::move(ud)});
        io_uring_prep_read(sqe,fd,buf_p,size,offset);
        sqe->user_data=key;//手动处理的，不转成指针，通过map查询可以使得缓冲区受到map管理

        count_result++;
        return true;
    };

    unique_result async_io::write(int fd,unique_buf&& buf,uint32_t size,uint64_t user_data,uint64_t offset){
        submit_error=false;
        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return nullptr;
        
        uint64_t key = id++;
        use_data ud{key,user_data,std::move(buf),RW::WRITE};
        uint8_t* buf_p = ud.buf.get();

        map->insert({key,std::move(ud)});
        io_uring_prep_write(sqe,fd,buf_p,size,offset);
        sqe->user_data=key;//手动处理的，不转成指针，通过map查询可以使得缓冲区受到map管理

        if(io_uring_submit(ring.get())<0){
            count_result++;
            submit_error=true;
            return nullptr;
        }
        return std::make_unique<async_result>(ring,map);
    };

    bool async_io::prep_write(int fd,unique_buf&& buf,uint32_t size,uint64_t user_data,uint64_t offset){
        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return false;
        
        uint64_t key = id++;
        use_data ud{key,user_data,std::move(buf),RW::WRITE};
        uint8_t* buf_p = ud.buf.get();

        map->insert({key,std::move(ud)});
        io_uring_prep_write(sqe,fd,buf_p,size,offset);
        sqe->user_data=key;//手动处理的，不转成指针，通过map查询可以使得缓冲区受到map管理

        count_result++;
        return true;
    };

    size_t async_io::result_count(){
        return count_result;
    };

    size_t async_io::queue_size(){
        return map->size();
    };

    unique_result async_io::get_result(){
        if(count_result==0) return nullptr;
        count_result--;
        return std::make_unique<async_result>(ring,map);
    };

    bool async_io::submit(){
        submit_error=false;
        if(io_uring_submit(ring.get())<0){
            submit_error=true;
            return false;
        }
        return true;
    };

    bool async_io::submit_err(){
        return submit_error;
    };
};
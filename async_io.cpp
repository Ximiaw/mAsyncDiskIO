#ifndef ASYNC_IO
#define ASYNC_IO

#include<liburing.h>
#include<inttypes.h>
#include<vector>
#include<stdexcept>

class async_io;

class async_read_result{
    private:
    friend class async_io;
    io_uring_cqe* cqe;
    io_uring* ring;
    u_int8_t* buf;
    public:
    explicit async_read_result(io_uring* ring,size_t buf_size):ring(ring){
        buf=new u_int8_t[buf_size];
        cqe=nullptr;
    };
    ~async_read_result(){
        if(!ring) return;
        if(!cqe) wait();
        finish();
    };
    async_read_result(async_read_result&)=delete;
    async_read_result(async_read_result&& other) noexcept{
        cqe=other.cqe;
        ring=other.ring;
        buf=other.buf;
        other.cqe=nullptr;
        other.ring=nullptr;
        other.buf=nullptr;
    };
    async_read_result& operator=(async_read_result&)=delete;
    async_read_result& operator=(async_read_result&& other) noexcept{
        if(this==&other){
            return *this;
        }
        cqe=other.cqe;
        ring=other.ring;
        buf=other.buf;
        other.cqe=nullptr;
        other.ring=nullptr;
        other.buf=nullptr;
        return *this;
    };

    public:
    bool empty(){
        return ring==nullptr;
    };
    bool peek(){
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret==0 && cqe->res>=0){
            return true;
        }
        cqe=nullptr;
        return false;
    };
    bool wait(){
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret==0 && cqe->res>=0){
            return true;
        }
        cqe=nullptr;
        return false;
    };
    void* user_data(){
        if(!cqe) throw std::runtime_error("user_data:cqe is empty");
        return (void*)cqe->user_data;
    };
    u_int8_t* data(){
        if(!cqe) throw std::runtime_error("data:buffer is empty");
        return buf;
    };
    size_t size(){
        if(!cqe) throw std::runtime_error("size:cqe is empty");
        return cqe->res;
    };
    void finish(){
        if(!ring||!cqe) return;
        io_uring_cqe_seen(ring,cqe);
        if(buf) delete[] buf;
        ring=nullptr;
        cqe=nullptr;
        buf=nullptr;
    };
};

class async_io{
    private:
    io_uring ring;

    public:
    async_io(int deep=32){
        int ret = io_uring_queue_init(deep,&ring,IORING_SETUP_SQPOLL);
        if(ret<0) throw std::runtime_error("io_uring_queue_init failed");
    };
    ~async_io(){
        io_uring_queue_exit(&ring);
    };
    async_io(async_io&)=delete;
    async_io(async_io&&)=delete;
    async_io& operator=(async_io&)=delete;
    async_io& operator=(async_io&&)=delete;

    public:
    async_read_result read(int fd,size_t size,u_int64_t offset=0,void* user_data=nullptr){
        io_uring_sqe* sqe=io_uring_get_sqe(&ring);
        if(!sqe) return async_read_result{nullptr,0};//队列没有空位置失败

        async_read_result rs{&ring,size};
        io_uring_prep_read(sqe,fd,rs.buf,size,offset);
        io_uring_sqe_set_data(sqe,user_data);
        io_uring_submit(&ring);
        return rs;
    };
};

#endif
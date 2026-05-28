#ifndef ASYNC_IO
#define ASYNC_IO

#include<liburing.h>
#include<inttypes.h>
#include<vector>
#include<stdexcept>

struct user_data
{
    u_int64_t usr_d;
    u_int8_t* buf;
};

class async_read_result{
    private:
    io_uring_cqe* cqe;
    io_uring* ring;
    u_int8_t* buf;
    user_data* user_d;
    public:
    explicit async_read_result(io_uring* ring):ring(ring),buf(nullptr){
        cqe=nullptr;
    };
    ~async_read_result(){
        if(!ring){
            if(buf) delete[] buf;
            return;
        };
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
    int peek(){
        int ret = io_uring_peek_cqe(ring,&cqe);
        if(ret!=0){
            cqe=nullptr;
            return false;
        }
        buf=reinterpret_cast<u_int8_t*>(cqe->user_data);
        return cqe->res;
    };
    int wait(){
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0){
            cqe=nullptr;
            return false;
        }
        buf=reinterpret_cast<u_int8_t*>(cqe->user_data);
        return cqe->res;
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
        int ret = io_uring_queue_init(deep,&ring,0);//IORING_SETUP_SQPOLL
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
        if(!sqe) return async_read_result{nullptr};//队列没有空位置失败

        async_read_result rs{&ring};
        u_int8_t* buf=new u_int8_t[size];
        io_uring_prep_read(sqe,fd,buf,size,offset);
        io_uring_sqe_set_data(sqe,buf);
        io_uring_submit(&ring);
        return rs;
    };
};

#endif
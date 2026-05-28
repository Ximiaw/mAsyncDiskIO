#ifndef ASYNC_IO
#define ASYNC_IO

#include<liburing.h>
#include<inttypes.h>
#include<stdexcept>
#include<memory>
#include<set>

struct user_data
{
    u_int64_t usr_d;
    u_int8_t* buf;
};

class async_read_result{
    private:
    io_uring_cqe* cqe;
    io_uring* ring;
    user_data* user_d;
    std::set<async_read_result>* set;
    public:
    explicit async_read_result(io_uring* ring,std::set<async_read_result>* set):ring(ring),buf(nullptr),set(set){
        cqe=nullptr;
    };
    ~async_read_result(){
        if(!ring){
            if(user_d&&user_d->buf) delete[] user_d->buf;
            return;
        };
        if(!cqe) wait();
        finish();
        if(set&&set->find(*this)!=set->end())
            set->erase(*this);
    };
    async_read_result(async_read_result&)=delete;
    async_read_result(async_read_result&& other) noexcept{
        cqe=other.cqe;
        ring=other.ring;
        user_d=other.user_d;

        other.cqe=nullptr;
        other.ring=nullptr;
        other.user_d=nullptr;
        other.set=nullptr;
    };
    async_read_result& operator=(async_read_result&)=delete;
    async_read_result& operator=(async_read_result&& other) noexcept{
        if(this==&other){
            return *this;
        }
        cqe=other.cqe;
        ring=other.ring;
        user_d=other.user_d;

        other.cqe=nullptr;
        other.ring=nullptr;
        other.user_d=nullptr;
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
        user_d=reinterpret_cast<user_data*>(cqe->user_data);
        return cqe->res;
    };
    int wait(){
        int ret = io_uring_wait_cqe(ring,&cqe);
        if(ret!=0){
            cqe=nullptr;
            return false;
        }
        user_d=reinterpret_cast<user_data*>(cqe->user_data);
        return cqe->res;
    };
    u_int64_t use_data(){
        if(!cqe||!user_d||!user_d->usr_d) throw std::runtime_error("use_data:user_data is empty");
        return user_d->usr_d;
    };
    u_int8_t* data(){
        if(!cqe||!user_d||!user_d->buf) throw std::runtime_error("data:buffer is empty");
        return user_d->buf;
    };
    size_t size(){
        if(!cqe) throw std::runtime_error("size:cqe is empty");
        return cqe->res;
    };
    void finish(){
        if(!ring||!cqe) return;
        io_uring_cqe_seen(ring,cqe);
        if(user_d&&user_d->buf) delete[] user_d->buf;
        ring=nullptr;
        cqe=nullptr;
        user_d=nullptr;
    };
};

class async_io{
    private:
    using shared_read=std::shared_ptr<async_read_result>;
    using weak_read=std::weak_ptr<async_read_result>;
    private:
    io_uring ring;
    std::set<shared_read> set;

    public:
    async_io(int deep=32){
        int ret = io_uring_queue_init(deep,&ring,0);//IORING_SETUP_SQPOLL
        if(ret<0) throw std::runtime_error("io_uring_queue_init failed");
    };
    ~async_io(){
        io_uring_queue_exit(&ring);
        set.clear();
    };
    async_io(async_io&)=delete;
    async_io(async_io&&)=delete;
    async_io& operator=(async_io&)=delete;
    async_io& operator=(async_io&&)=delete;

    public:
    weak_read read(int fd,size_t size,u_int64_t offset=0,u_int64_t use_data=0){
        io_uring_sqe* sqe=io_uring_get_sqe(&ring);
        if(!sqe) return weak_read{};//队列没有空位置失败

        shared_read rs=std::make_shared<async_read_result>(&ring,&set);
        user_data* buf=new user_data{use_data,new u_int8_t[size]};
        io_uring_prep_read(sqe,fd,buf,size,offset);
        io_uring_sqe_set_data(sqe,buf);
        io_uring_submit(&ring);
        return rs;
    };
};

#endif
#include"async_io.h"

namespace mAsyncDiskIO{

    async_io::async_io(size_t deep=32){
        int ret = io_uring_queue_init(deep,&ring,0);//IORING_SETUP_SQPOLL
        if(ret<0) throw std::runtime_error("io_uring_queue_init failed");
    };

    async_io::~async_io(){
        set.clear();
        io_uring_queue_exit(&ring);
    };

    weak_result async_io::read(int fd,size_t size,uint64_t offset,uint64_t user_data){
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if(!sqe) return weak_result{};//队列没有空位置失败

        shared_result sr = std::make_shared<async_result_base>(new async_result_read{&ring,&set});
        async_result_read* arr = static_cast<async_result_read*>(sr.get());
        arr->weak_r = sr;

        use_data* ud = new use_data{user_data,new uint8_t[size]};
        io_uring_prep_read(sqe,fd,ud->buf,size,offset);
        io_uring_sqe_set_data(sqe,ud);
        io_uring_submit(&ring);
        
        set.insert(sr);
        return sr;
    };

};
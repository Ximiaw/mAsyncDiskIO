#include"async_io.h"

namespace mAsyncDiskIO{

    async_io::async_io(size_t deep):ring(new io_uring){
        int ret = io_uring_queue_init(deep,ring.get(),IORING_SETUP_SQPOLL);
        if(ret<0) throw std::runtime_error("io_uring_queue_init failed");
    };

    async_io::~async_io(){
        io_uring_queue_exit(ring.get());
    };

    shared_result_read async_io::read(int fd,size_t size,uint64_t offset,uint64_t user_data){
        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return shared_result_read{};//队列没有空位置失败

        shared_result_read sr = std::make_shared<async_result_read>(ring);
        async_result_read* arr = sr.get();

        use_data* ud = new use_data{user_data,new uint8_t[size+1]{}};
        io_uring_prep_read(sqe,fd,ud->buf,size,offset);
        io_uring_sqe_set_data(sqe,ud);
        io_uring_submit(ring.get());
        
        return sr;
    };

    shared_result_write async_io::write(int fd,unique_buf&& buf,size_t size,uint64_t offset,uint64_t user_data){
        io_uring_sqe* sqe = io_uring_get_sqe(ring.get());
        if(!sqe) return shared_result_write{};//队列没有空位置失败

        shared_result_write sr = std::make_shared<async_result_write>(ring);
        async_result_write* arw = sr.get();

        use_data* ud = new use_data{user_data,buf.release()};
        io_uring_prep_write(sqe,fd,ud->buf,size,offset);
        io_uring_sqe_set_data(sqe,ud);
        io_uring_submit(ring.get());

        return sr;
    };

};
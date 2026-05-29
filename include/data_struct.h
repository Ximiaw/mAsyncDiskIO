#ifndef DATA_STRUCT_H
#define DATA_STRUCT_H

#include<inttypes.h>
#include<optional>
#include<memory>
#include<set>
#include"result_base.h"

namespace mAsyncDiskIO{
    class async_result_read;
    class async_result_write;

    struct use_data
    {
        uint64_t ues_d=0;//用户数据，用于标识，如果是指针则其生命周期由外界保证
        uint8_t* buf=nullptr;

        ~use_data(){
            if(buf) delete[] buf;
        };
    };

    using shared_result=std::shared_ptr<async_result_base>;
    using shared_result_read=std::shared_ptr<async_result_read>;
    using shared_result_write=std::shared_ptr<async_result_write>;
    using weak_result_read=std::weak_ptr<async_result_read>;
    using weak_result_write=std::weak_ptr<async_result_write>;
    using result_set=std::set<shared_result>;

    using optional_ui64=std::optional<uint64_t>;

    using unique_buf=std::unique_ptr<uint8_t[],void(*)(uint8_t*)>;
    inline unique_buf make_unique_buf(uint8_t* ptr=nullptr){
        return unique_buf{ptr,[](uint8_t* ptr){
            if(ptr) delete[] ptr;
        }};
    };

    enum class state{
        ERROR,
        FINISH,
        UNFINISHED
    };
};

#include"result_read.h"
#include"result_write.h"

#endif //DATA_STRUCT_H
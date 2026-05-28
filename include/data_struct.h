#ifndef DATA_STRUCT_H
#define DATA_STRUCT_H

#include<inttypes.h>
#include<memory>
#include<set>
#include"result_base.h"

namespace mAsyncDiskIO{
    struct use_data
    {
        uint64_t ues_d=0;//用户数据，用于标识，如果是指针则其生命周期由外界保证
        uint8_t* buf=nullptr;

        ~use_data(){
            if(buf) delete[] buf;
        };
    };

    using shared_result=std::shared_ptr<async_result_base>;
    using weak_result=std::weak_ptr<async_result_base>;
    using result_set=std::set<shared_result>;

};

#endif //DATA_STRUCT_H
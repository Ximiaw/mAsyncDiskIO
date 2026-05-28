#ifndef DATA_STRUCT_H
#define DATA_STRUCT_H

#include<inttypes.h>
#include<memory>
#include<set>

namespace mAsyncDiskIO{
    struct user_data
    {
        uint64_t ues_d=0;//用户数据，用于标识，生命周期由外界保证
        uint8_t* buf=nullptr;

        ~user_data(){
            if(buf) delete[] buf;
        };
    };

    

};

#endif //DATA_STRUCT_H
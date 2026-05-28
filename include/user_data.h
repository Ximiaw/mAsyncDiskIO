#ifndef USER_DATA_H
#define USER_DATA_H

#include<inttypes.h>

namespace mAsyncDiskIO{
    struct user_data
    {
        uint64_t ues_d=0;
        uint8_t* buf=nullptr;

        ~user_data(){
            if(buf) delete[] buf;
        };
    };
};

#endif //USER_DATA_H
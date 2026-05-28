#include"async_io.h"
#include"result_read.h"
#include<stdio.h>
#include<fcntl.h>

int main(){
    int fd = open("/home/ximi/data/code/mAsyncDiskIO/CMakeLists.txt",O_RDONLY);
    mAsyncDiskIO::async_io ai{};
    mAsyncDiskIO::weak_result wr = ai.read(fd,1024,0,0);
    mAsyncDiskIO::async_result_read* arr = static_cast<mAsyncDiskIO::async_result_read*>(wr.lock().get());
    printf("%ld\n",arr->wait());
    printf("%s\n",arr->data());
};
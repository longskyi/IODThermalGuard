#ifndef IOD_UTILS_HEADER
#define IOD_UTILS_HEADER

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <thread>
#include <Windows.h>
#include <hwisenssm2.h>
#include <iostream>
#include <mutex>
#include <atomic>
#include "monitorUtils.h"
#include "logUtils.h"
#include <format>
#include <string>
#define WORKER_BUFFER_SIZE (64)



struct IOD_performance_settings
{
    double IOD_high;
    double IOD_target_high;
    double IOD_low;
};

class HWINFOdataFetcher
{
private:
    std::string sensor_name;
    std::shared_ptr<CircularBuffer<long long,double>> sensor_data_buffer;
    std::atomic<double> sensor_value;
    std::atomic<long long> sensor_time;
    std::thread HWIworker_thread;
    void (*logger) (std::string logger_str,LogLevel level); 
    bool get_data_temp_run=false;
    HANDLE hHWiNFOMemory; // hwinfo共享内存handle
    DWORD dwIODReading; //IOD 数据偏移量
    int data_invalid_status; // 0 for valid , 1 for DEAD , 2 for overtime
public:

    HWINFOdataFetcher(std::string _sensor_name);
    HWINFOdataFetcher(std::string _sensor_name , void (*_logger) (std::string logger_str,LogLevel level));
    HWINFOdataFetcher(const HWINFOdataFetcher & other) = delete;
    int init(); // 0 for success , 1 for failed
    void worker_thread();
    std::shared_ptr<CircularBuffer<long long,double>> get_buffer_ptr();
    // 0 for valid , 1 for DEAD , 2 for overtime
    int get_data_invalid_status();
    void join();
};

#endif
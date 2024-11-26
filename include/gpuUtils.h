#ifndef GPU_UTILS_HEADER
#define GPU_UTILS_HEADER
#include <mutex>
#include <iostream>
#include <nvml.h>
#include <memory>
#include "monitorUtils.h"
#include "iodUtils.h"
#include <string>
#include <algorithm>
#include <thread>
#include <functional>

struct GPU_performance_settings
{
    unsigned int GPU_minCLK;
    unsigned int GPU_maxCLK;
    unsigned int MAX_TGP_mw;
    unsigned int MAX_Frequncy_Mhz;
};



class GPU_monitor_data
{
public:
    GPU_monitor_data(unsigned int GPU_Temp,unsigned int GPU_power,unsigned int GPU_clkMhz,nvmlUtilization_t _GPU_Usage) :
    GPU_Temp(GPU_Temp),GPU_power(GPU_power),GPU_clkMhz(GPU_clkMhz)
    {
        this->GPU_Usage.gpu = _GPU_Usage.gpu;
        this->GPU_Usage.memory = _GPU_Usage.memory;
    }
    GPU_monitor_data()
    {
        this->GPU_Temp =0; this->GPU_power=0; this->GPU_clkMhz=0;
        this->GPU_Usage.gpu=0; this->GPU_Usage.memory=0;
    };
    GPU_monitor_data& operator+=(const GPU_monitor_data& other)
    {
        this->GPU_Temp += other.GPU_Temp;
        this->GPU_power += other.GPU_power;
        this->GPU_clkMhz += other.GPU_clkMhz;
        this->GPU_Usage.gpu += other.GPU_Usage.gpu;
        this->GPU_Usage.memory += other.GPU_Usage.memory;
        return *this;
    }
    GPU_monitor_data operator+(const GPU_monitor_data& other) const
    {
        GPU_monitor_data result = *this;
        result += other;
        return result;
    }
    GPU_monitor_data& operator/=(int num)
    {
        this->GPU_Temp /= num;
        this->GPU_power /= num;
        this->GPU_clkMhz /= num;
        this->GPU_Usage.gpu /= num;
        this->GPU_Usage.memory /= num;
        return *this;
    }
    GPU_monitor_data operator/(int num) const
    {
        GPU_monitor_data result = *this;
        result /= num;
        return result;
    }
    unsigned int GPU_Temp;
    unsigned int GPU_power;
    unsigned int GPU_clkMhz;
    nvmlUtilization_t GPU_Usage;
};


class GPUController
{
private:
    std::atomic<long long> GPU_data_record_id; //default to zero
    std::shared_ptr<CircularBuffer<long long,GPU_monitor_data>> GPU_data_buffer;
    std::mutex critical_func_lock_GPU;
    void (*logger) (std::string logger_str,LogLevel level);
public:
    GPUController(const GPUController & other) = delete;
    GPUController(nvmlDevice_t _gpu_device,struct GPU_performance_settings gpu_setting,void (*_logger) (std::string logger_str,LogLevel level));
    nvmlDevice_t gpu_device;

    // gpu performance profile
    unsigned int GPU_minCLK;
    unsigned int GPU_maxCLK;
    
    unsigned int GPUlgc(unsigned int targetmax,int src,int behave); //behave : 0 for reset , 1 for set
    GPU_monitor_data fetch_GPU_status();
    void GPU_worker_info_get();
    std::shared_ptr<CircularBuffer<long long,GPU_monitor_data>> get_buffer_ptr();
};


#endif
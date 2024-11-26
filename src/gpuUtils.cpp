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
#include <gpuUtils.h>


GPUController::GPUController(nvmlDevice_t _gpu_device,struct GPU_performance_settings gpu_setting,void (*_logger) (std::string logger_str,LogLevel level))
{
    gpu_device = _gpu_device;
    logger = _logger;
    GPU_data_record_id = 0;
    GPU_minCLK = gpu_setting.GPU_minCLK;
    GPU_maxCLK = gpu_setting.GPU_maxCLK;
}

unsigned int GPUController::GPUlgc(unsigned int targetmax,int src,int behave) //behave : 0 for reset , 1 for set
{
    std::unique_lock<std::mutex> lock(critical_func_lock_GPU);
    //printf("tring to set to %dMhz from src %d\n",targetmax,src);
    if(targetmax<GPU_minCLK || targetmax>GPU_maxCLK)
    {
        std::cerr<<"detected error target GPU_frequnecy from src"<<src<<std::endl;
        targetmax=(GPU_minCLK+GPU_maxCLK)/2;
    }
    nvmlReturn_t nvmlflag3=NVML_SUCCESS;

    if(behave) 
    {
        nvmlflag3 = nvmlDeviceSetGpuLockedClocks (gpu_device, 0, targetmax);
    }
    else
    {
        nvmlflag3 = nvmlDeviceResetGpuLockedClocks (gpu_device);
    }

    if(nvmlflag3!=NVML_SUCCESS)
    {
        std::cerr<<"set GPU LockedCLocks error "<<nvmlErrorString(nvmlflag3)<<std::endl;
        // system(PROC_NVIDIASMI_STR);
    }
    return targetmax;
    //nvmlDeviceSetGpuLockedClocks ( nvmlDevice_t device, unsigned int  minGpuClockMHz, unsigned int  maxGpuClockMHz )
    //nvmlDeviceResetGpuLockedClocks ( nvmlDevice_t device )
}


GPU_monitor_data GPUController::fetch_GPU_status()
{
    std::unique_lock<std::mutex> lock(critical_func_lock_GPU);

    nvmlReturn_t nvml_flag1[10];
    unsigned int GPU_Temp=-1,GPU_power=-1,GPU_clock=-1;
    nvmlUtilization_t GPU_Usage;
    nvml_flag1[0] = nvmlDeviceGetTemperature(gpu_device,NVML_TEMPERATURE_GPU,&GPU_Temp);
    nvml_flag1[1] = nvmlDeviceGetPowerUsage(gpu_device,&GPU_power);
    nvml_flag1[2] = nvmlDeviceGetClock(gpu_device,NVML_CLOCK_GRAPHICS,NVML_CLOCK_ID_CURRENT,&GPU_clock);
    nvml_flag1[3] = nvmlDeviceGetUtilizationRates(gpu_device,&GPU_Usage);

    for(int i=0;i<=3;i++)
    {
        if(nvml_flag1[i]!=NVML_SUCCESS)
        {
            logger(std::string(nvmlErrorString(nvml_flag1[i])),LogLevel::ERRO); //ERROR
        }
    }
    return GPU_monitor_data(GPU_Temp,GPU_power,GPU_clock,GPU_Usage);
}

void GPUController::GPU_worker_info_get()
{
    GPU_monitor_data GPU_if = fetch_GPU_status();
    GPU_data_buffer->write(this->GPU_data_record_id++,GPU_if);
}


std::shared_ptr<CircularBuffer<long long,GPU_monitor_data>> GPUController::get_buffer_ptr()
{
    return this->GPU_data_buffer;
}
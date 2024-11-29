#ifndef THERMAL_CONTROL_HEADER
#define THERMAL_CONTROL_HEADER

#include <mutex>
#include <iostream>
#include <nvml.h>
#include <memory>
#include <string>
#include <algorithm>
#include <thread>
#include <functional>
#include "gpuUtils.h"
#include "monitorUtils.h"
#include "iodUtils.h"
#include "logUtils.h"
#include "P_watchdog.h"
#include "INIReader.h"
#ifndef WORKER_BUFFER_SIZE
#define WORKER_BUFFER_SIZE (64)
#endif
#ifndef GPU_ADJUST_INTERVAL_SEC
#define GPU_ADJUST_INTERVAL_SEC (3)
#endif

inline const std::string embeddedConfigFileContent = R"(
[GPUPerformanceSettings]
GPU_maxCLK = 3000          ; The maximum frequency limit (in MHz) that the software can adjust the GPU to.
GPU_minCLK = 600           ; The minimum frequency limit (in MHz) for the GPU.
MAX_Frequency_Mhz = 2580   ; The actual maximum frequency (in MHz) that the GPU can achieve.
MAX_TGP_mw = 175000        ; The maximum power consumption (in milliwatts) of the GPU.

[IODPerformanceSettings]
IOD_high = 87.0            ; The IOD temperature threshold (in °C) that triggers overheating protection.
IOD_target_high = 83.5     ; The target temperature (in °C) for the IOD.
IOD_low = 78                ; The IOD temperature (in °C) below which control will stop.
)";


void read_settings(IOD_performance_settings & iod_settings, GPU_performance_settings & gpu_settings);


class IOD_GPU_ThermalManager
{
private:
    GPUController & GPU_controller;
    HWINFOdataFetcher & IODfetcher;
    std::shared_ptr<CircularBuffer<time_t,double> > IOD_data;
    void (*logger) (std::string logger_str,LogLevel level);
    // performance settings
    unsigned int GPU_minCLK;
    unsigned int GPU_maxCLK;
    unsigned int MAX_TGP_mw;
    unsigned int MAX_Frequncy_Mhz;
    double IOD_high;
    double IOD_target_high;
    double IOD_low;
    //control paramater
    std::atomic<bool> GPU_worker_run;
    std::atomic<bool> IOD_PROC_run;
    std::thread IOD_PROC_th;
    std::thread GPU_worker_th;
public:
    // output control paramater
    std::atomic<bool> IOD_PROCHOT; // status flag
    std::atomic<bool> GPU_now_control; // status flag
    // for output
    int GPU_now_maxCLK;
    int targetMhz_offset=0;
    IOD_GPU_ThermalManager(const IOD_GPU_ThermalManager & other) = delete;
    inline IOD_GPU_ThermalManager( GPUController & _GPU_control,
                            HWINFOdataFetcher & _IODfetcher,
                            struct GPU_performance_settings GPU_setting , 
                            struct IOD_performance_settings IOD_setting ,
                            void (*_logger) (std::string logger_str,LogLevel level)) : GPU_controller(_GPU_control),IODfetcher(_IODfetcher)
    {
        IOD_data = IODfetcher.get_buffer_ptr();
        logger = _logger;

        GPU_maxCLK =  GPU_setting.GPU_maxCLK;
        GPU_minCLK = GPU_setting.GPU_minCLK;
        MAX_Frequncy_Mhz = GPU_setting.MAX_Frequency_Mhz;
        MAX_TGP_mw = GPU_setting.MAX_TGP_mw;
        IOD_high = IOD_setting.IOD_high;
        IOD_low = IOD_setting.IOD_low;
        IOD_target_high = IOD_setting.IOD_target_high;
        
        IOD_PROC_run=true;
        IOD_PROC_th  = std::thread(std::bind(&IOD_GPU_ThermalManager::IOD_PROC_thread,this));
        GPU_worker_run=true;
        GPU_worker_th = std::thread(std::bind(&IOD_GPU_ThermalManager::Thermal_worker_thread,this));

        IOD_PROCHOT=false;
        GPU_now_control=false;
        GPU_now_maxCLK = GPU_maxCLK;
        
    }
    inline void join()
    {
        GPU_worker_run = false;
        IOD_PROC_run = false;
        IOD_PROC_th.join();
        GPU_worker_th.join();
    }
    void Thermal_worker_thread();
    void IOD_PROC_thread();
};

    



#endif
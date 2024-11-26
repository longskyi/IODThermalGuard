#include <iostream>
#include "monitorUtils.h"
#include "thermalControl.cpp"
#include "iodUtils.h"
#include "P_watchdog.h"
#include <signal.h>

std::atomic<bool> main_Terminate=false;
void SIGINT_Handler(int signum)
{
    if(signum==SIGINT)
    {
        std::cout<<"Interrupt signal received, terminating Process"<<std::endl;
        main_Terminate=true;
    }
    return;
}


void stdout_logger (std::string logger_str,LogLevel level)
{
    std::cout<<logger_str<<std::endl;
    return;
} 

int main()
{   
    signal(SIGINT,SIGINT_Handler);
    SetConsoleOutputCP(65001);  
    SetConsoleCP(65001);  
    
    nvmlReturn_t nvml_flag;
    unsigned int Unit_count=1;
    unsigned int Device_count;
    
    nvml_flag=nvmlInit_v2();
    if(nvml_flag!=NVML_SUCCESS)
    {
        std::cout<<nvmlErrorString(nvml_flag)<<std::endl;
        exit(0);
    }
    

    nvmlDeviceGetCount_v2(&Device_count);
    if(Device_count <1)
    {
        printf("%d %d",Unit_count,Device_count);
        std::cerr<<"device less than 1/cannot find device"<<std::endl;
        nvmlShutdown();
        return 0;
    }
    nvmlDevice_t gpu_Device;
    nvmlDeviceGetHandleByIndex_v2(0,&gpu_Device);

    struct GPU_performance_settings gpu_s;
    gpu_s.GPU_maxCLK = 3000;
    gpu_s.GPU_maxCLK = 600;
    gpu_s.MAX_Frequncy_Mhz = 2580;
    gpu_s.MAX_TGP_mw = 175000;
    struct IOD_performance_settings iod_s;
    iod_s.IOD_high = 87;
    iod_s.IOD_low = 78;
    iod_s.IOD_target_high =83.5;

    auto iod_fetcher = HWINFOdataFetcher("IOD Hot Spot",stdout_logger);
    auto gpu_control = GPUController(gpu_Device,gpu_s,stdout_logger);
    auto thermalman = IOD_GPU_ThermalManager(gpu_control,iod_fetcher,gpu_s,iod_s,stdout_logger);
    P_watchdog::watchdog_start();
    while(1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        if(main_Terminate) break;
        // bool ism=isMonitored,ism2=GPU_now_control;
        time_t rawtime;  
        struct tm timeinfo;  
        time(&rawtime);
        double IOD_Temp = iod_fetcher.get_buffer_ptr()->get_Lastest_data();
        auto tmpa = gpu_control.fetch_GPU_status();
        int GPU_power = tmpa.GPU_power;
        int GPU_clock = tmpa.GPU_clkMhz;
        int GPU_now_maxCLK = thermalman.GPU_now_maxCLK;
        bool ism2 = thermalman.GPU_now_control;  
        localtime_s(&timeinfo,&rawtime);      
        printf("%02d:%02d:%02d IOD_TEMP:%f GPU_POWER:%d CLK %d target:%d NOW_CONTROL: %d\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,IOD_Temp,GPU_power,GPU_clock,GPU_now_maxCLK,ism2);
    }
    

    //join
    P_watchdog::watchdog_join();
    iod_fetcher.join();
    thermalman.join();
    system("nvidia-smi -rgc");
    nvmlShutdown();
    std::cout<<"GPU_MAX_CLOCK control has been Reset"<<std::endl;
    std::cout<<"terminate success";
    return 0;
}
#ifndef THERMAL_CONTROL_CPP
#define THERMAL_CONTROL_CPP
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
#include "thermalControl.h"

void read_settings(IOD_performance_settings & iod_settings, GPU_performance_settings & gpu_settings)
{
    std::u8string exePath = ::getExecutablePath();
    std::filesystem::path exeDirPath = std::filesystem::path(exePath).parent_path();

    std::string ini_name("IODThermalGuard.ini");
    
    std::filesystem::path iniFilePath = exeDirPath / ini_name;
    
    std::ifstream _file(iniFilePath);

    if(!_file.good())
    {
        std::ofstream ini_fstream(iniFilePath,std::ios::out);
        if(!ini_fstream.is_open())
        {
            std::cerr<<"Unable to create ini file , exiting\n";
            system("pause");
            _file.close();
            exit(0);
        }
        ini_fstream<<embeddedConfigFileContent;
        ini_fstream.close();
    }
    _file.close();
    
    std::ifstream ini_file(iniFilePath);
    if(!ini_file.good())
    {
        std::cerr<<"Unable to open ini file , exiting\n";
        system("pause");
        exit(0);
    }
    ini_file.close();

    FILE * ini_FILE;
    _wfopen_s(&ini_FILE,iniFilePath.wstring().c_str(),L"r");
    
    INIReader reader(ini_FILE);
 
    if (reader.ParseError() != 0) {
        std::cout << "Cannot load ini file , exiting\n";
        system("pause");
        exit(0);
    }
    
    gpu_settings.GPU_maxCLK = reader.GetInteger("GPUPerformanceSettings", "GPU_maxCLK", -1);
    gpu_settings.GPU_minCLK = reader.GetInteger("GPUPerformanceSettings", "GPU_minCLK", -1);
    gpu_settings.MAX_Frequency_Mhz = reader.GetInteger("GPUPerformanceSettings", "MAX_Frequency_Mhz", -1);
    gpu_settings.MAX_TGP_mw = reader.GetInteger("GPUPerformanceSettings", "MAX_TGP_mw", -1);
     
    iod_settings.IOD_high = (double) reader.GetFloat("IODPerformanceSettings", "IOD_high", -1.0f);
    iod_settings.IOD_low = (double) reader.GetFloat("IODPerformanceSettings", "IOD_low", -1.0f);
    iod_settings.IOD_target_high = (double) reader.GetFloat("IODPerformanceSettings", "IOD_target_high", -1.0f);


    std::cout << "GPU_maxCLK: " << gpu_settings.GPU_maxCLK << std::endl;
    std::cout << "GPU_minCLK: " << gpu_settings.GPU_minCLK << std::endl;
    std::cout << "MAX_Frequency_Mhz: " << gpu_settings.MAX_Frequency_Mhz << std::endl;
    std::cout << "MAX_TGP_mw: " << gpu_settings.MAX_TGP_mw << std::endl;
    std::cout << "IOD_high: " << iod_settings.IOD_high << std::endl;
    std::cout << "IOD_low: " << iod_settings.IOD_low << std::endl;
    std::cout << "IOD_target_high: " << iod_settings.IOD_target_high << std::endl;
    
    fclose(ini_FILE);
    // Error check
    if (gpu_settings.MAX_TGP_mw < 0 || gpu_settings.GPU_maxCLK < 0 || gpu_settings.GPU_minCLK < 0 || gpu_settings.MAX_Frequency_Mhz < 0) {
        std::cerr << "Detected error settings with value less than 0, terminating process" << std::endl;
        system("pause");
        exit(0);
    }
 
    if (iod_settings.IOD_low >= iod_settings.IOD_target_high || iod_settings.IOD_target_high >= iod_settings.IOD_high) {
        std::cerr << "Error: IOD settings do not satisfy the rule: IOD_low < IOD_target_high < IOD_high" << std::endl;
        system("pause");
        exit(0);
    }
 
    if (gpu_settings.GPU_minCLK >= gpu_settings.GPU_maxCLK) {
        std::cerr << "Error: GPU_minCLK should be less than GPU_maxCLK" << std::endl;
        system("pause");
        exit(0);
    }
 
    if (gpu_settings.GPU_minCLK >= gpu_settings.MAX_Frequency_Mhz) {
        std::cerr << "Error: GPU_minCLK should be less than MAX_Frequency_Mhz" << std::endl;
        system("pause");
        exit(0);
    }
 
    if (gpu_settings.GPU_minCLK <= 400 || gpu_settings.GPU_maxCLK <= 400 || gpu_settings.MAX_Frequency_Mhz <= 400) {
        std::cerr << "Error: GPU_minCLK, GPU_maxCLK, and MAX_Frequency_Mhz should be greater than 400" << std::endl;
        system("pause");
        exit(0);
    }
 
    // Warning message
    if (iod_settings.IOD_high - iod_settings.IOD_target_high < 2.0) {
        std::cout << "Warning: IOD_high - IOD_target_high is less than 2.0" << std::endl;
    }
 
    if (iod_settings.IOD_target_high - iod_settings.IOD_low < 4.0) {
        std::cout << "Warning: IOD_target_high - IOD_low is less than 4.0" << std::endl;
    }
 
    if (gpu_settings.GPU_minCLK < 600 || gpu_settings.GPU_maxCLK < 600 || gpu_settings.MAX_Frequency_Mhz < 600) {
        std::cout << "Warning: GPU_minCLK, GPU_maxCLK, or MAX_Frequency_Mhz is less than 600" << std::endl;
    }

    return;
}



void IOD_GPU_ThermalManager::Thermal_worker_thread()
{
    while(GPU_worker_run)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(!GPU_now_control) continue;
    
        CircularBuffer<long long,GPU_monitor_data> GPU_ifbuffer(WORKER_BUFFER_SIZE);
        CircularBuffer<long long,GPU_monitor_data> GPU_worker_culculate_buffer(WORKER_BUFFER_SIZE);
        long long record_id=0;
        long long culculate_id=0;
        unsigned int targetMHz=GPU_minCLK;
        double IOD_Integer=0; //积分
        while(GPU_now_control && GPU_worker_run)
        {
            for(int i=0;i<4*GPU_ADJUST_INTERVAL_SEC;i++)
            {
                auto tmp = GPU_controller.fetch_GPU_status();
                GPU_ifbuffer.write(++record_id,tmp);
                std::this_thread::sleep_for(std::chrono::milliseconds(230));
            }

            culculate_id++;
            GPU_monitor_data GPU_if_avg;
            unsigned int rec_targetMHz=0;
            for(int i=0;i<4*GPU_ADJUST_INTERVAL_SEC;i++)
            {
                rec_targetMHz=std::max(rec_targetMHz,GPU_ifbuffer.read(record_id-i).GPU_clkMhz);
                GPU_if_avg += GPU_ifbuffer.read(record_id-i);
            }
            GPU_if_avg /= 4*GPU_ADJUST_INTERVAL_SEC;
            GPU_worker_culculate_buffer.write(culculate_id,GPU_if_avg);
            
            if(culculate_id == 1) continue; // init first data record

            time_t cur_time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            double cur_iod_TEMP=0,pre_iod_TEMP=0;
            for(int i=0;i<GPU_ADJUST_INTERVAL_SEC;i++)
            {
                cur_iod_TEMP+=IOD_data->read(cur_time-i);
                pre_iod_TEMP+=IOD_data->read(cur_time-i-GPU_ADJUST_INTERVAL_SEC);
            }
            GPU_monitor_data cur_GPU_if_avg=GPU_worker_culculate_buffer.read(culculate_id);
            GPU_monitor_data pre_GPU_if_avg=GPU_worker_culculate_buffer.read(culculate_id-1);
            cur_iod_TEMP/=(double) GPU_ADJUST_INTERVAL_SEC;
            pre_iod_TEMP/=(double) GPU_ADJUST_INTERVAL_SEC;
            
            // adjust algorithm
            bool IOD_high = cur_iod_TEMP > IOD_target_high;
            double GPU_power_incresed = cur_GPU_if_avg.GPU_power - pre_GPU_if_avg.GPU_power;

            if(GPU_power_incresed>0) GPU_power_incresed=0;

            double IOD_derivative=cur_iod_TEMP-pre_iod_TEMP;
            double IOD_difference=IOD_target_high-cur_iod_TEMP;

            targetMhz_offset=0;

            if(IOD_high||cur_GPU_if_avg.GPU_Usage.gpu>75)
            {
                targetMHz=rec_targetMHz;
            }
            if(IOD_high)
            {
                IOD_Integer+= (-IOD_difference);
                targetMhz_offset=static_cast<int>(-75.0*((-IOD_difference)*1.0 + 4.0*IOD_derivative + (MAX_Frequncy_Mhz*GPU_power_incresed/MAX_TGP_mw)/60.0 + 0.5*IOD_Integer));
                if(targetMhz_offset>0) targetMhz_offset=0;
            }
            else
            {
                IOD_Integer=0;
                targetMhz_offset=static_cast<int>(50.0*(IOD_difference*1.0 - 6.0*IOD_derivative));
            }
            if(std::abs(((double)targetMhz_offset)/((double)targetMHz))>0.2)
            {
                targetMHz=static_cast<unsigned int>((targetMhz_offset>0)?(1.2*targetMHz):(0.8*targetMHz));
            }
            else
            {
                targetMHz=targetMHz+targetMhz_offset;
            }
            if(targetMHz<GPU_minCLK)   targetMHz=GPU_minCLK;
            if(targetMHz>GPU_maxCLK)    targetMHz=GPU_maxCLK;

            //adjust algorithm end
            if((!IOD_PROCHOT) && GPU_now_control && GPU_worker_run)
            {
                targetMHz = GPU_controller.GPUlgc(targetMHz,1,1);
                logger(std::string(std::format( "Tring to set to {} Mhz , offset {} Mhz",
                                                targetMHz,targetMHz - rec_targetMHz)),LogLevel::DEBUG);
            }
            GPU_now_maxCLK=static_cast<int>(targetMHz);
            P_watchdog::feedWatchdog("gpu_worker");
        }
    }
    return;
}

void IOD_GPU_ThermalManager::IOD_PROC_thread()
{
    P_watchdog::set("IOD_PROC");
    IOD_PROCHOT = false;
    GPU_now_control = false;
    while(IOD_PROC_run)
    {
        P_watchdog::feedWatchdog("IOD_PROC");
        double cur_IOD_temp;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        cur_IOD_temp = IODfetcher.get_buffer_ptr()->get_Lastest_data();
        int IOD_data_status =  IODfetcher.get_data_invalid_status();
        bool prev_IOD_PROCHOT = IOD_PROCHOT;
        bool prev_GPU_now_control = GPU_now_control;
        //update IOD_PROCHOT flag and GPU_now_control flag
        //reserve
        if(IOD_data_status !=0 )
        {
            IOD_PROCHOT = true;
            GPU_now_control = false;
        }
        else if(prev_IOD_PROCHOT) {
            if(cur_IOD_temp<IOD_high-5)
            {
                IOD_PROCHOT=false;
                if(cur_IOD_temp >= IOD_low && cur_IOD_temp <IOD_high) {
                    GPU_now_control = true;
                }
                else {
                    GPU_now_control = false;
                }
            }
            else
            {
                IOD_PROCHOT = true;
                GPU_now_control = false;
            }
        }
        else {
            if(cur_IOD_temp>=IOD_high) {
                IOD_PROCHOT = true;
                GPU_now_control = false;
            }
            else if(cur_IOD_temp >= IOD_low && cur_IOD_temp <IOD_high) {
                IOD_PROCHOT = false;
                GPU_now_control = true;
            }
            else {
                IOD_PROCHOT = false;
                GPU_now_control = false;
            }
        }
        
        if(prev_IOD_PROCHOT != IOD_PROCHOT)
        {
            if(prev_IOD_PROCHOT)
            {
                // true -> false
                this->GPU_controller.GPUlgc(GPU_maxCLK,0,0);// reset "rgc"
            }
            else
            {
                //false -> true
                logger("Trigger overheat protection , lock GPU frequency to GPU_minCLK",LogLevel::WARN);
                this->GPU_controller.GPUlgc(GPU_minCLK,0,1);
            }
        }
        
        if(prev_GPU_now_control != GPU_now_control)
        {
            if(prev_GPU_now_control)
            {
                //true -> false
                P_watchdog::unset("gpu_worker");
            }
            else
            {
                //false -> true
                P_watchdog::set("gpu_worker");
            }
        }

        if((prev_GPU_now_control || prev_IOD_PROCHOT) && ((!GPU_now_control) && (!IOD_PROCHOT))) {
            this->GPU_controller.GPUlgc(GPU_maxCLK,0,0);// reset "rgc"
        }
        
    }
    P_watchdog::unset("IOD_PROC");
}
    
#endif    







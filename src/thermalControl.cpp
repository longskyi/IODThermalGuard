#ifndef THERMAL_CONTROL_CPP
#define THERMAL_CONTROL_CPP
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
#define WORKER_BUFFER_SIZE (64)
#define GPU_ADJUST_INTERVAL_SEC (3)



class IOD_GPU_ThermalManager
{
private:
    GPUController & GPU_controller;
    HWINFOdataFetcher & IODfetcher;
    std::shared_ptr<CircularBuffer<time_t,double> > IOD_data;
    //control paramater
    std::atomic<bool> GPU_worker_run;
    std::atomic<bool> IOD_PROC_run;
    std::thread IOD_PROC_th;
    std::thread GPU_worker_th;
    void (*logger) (std::string logger_str,LogLevel level);
public:
    IOD_GPU_ThermalManager( GPUController & _GPU_control,HWINFOdataFetcher & _IODfetcher,
                            struct GPU_performance_settings GPU_setting , 
                            struct IOD_performance_settings IOD_setting ,
                            void (*logger) (std::string logger_str,LogLevel level)) : GPU_controller(_GPU_control),IODfetcher(_IODfetcher)
    {
        IOD_data = IODfetcher.get_buffer_ptr();
        GPU_maxCLK =  GPU_setting.GPU_maxCLK;
        GPU_minCLK = GPU_setting.GPU_minCLK;
        MAX_Frequncy_Mhz = GPU_setting.MAX_Frequncy_Mhz;
        MAX_TGP_mw = GPU_setting.MAX_TGP_mw;
        IOD_high = IOD_setting.IOD_high;
        IOD_low = IOD_setting.IOD_low;
        IOD_target_high = IOD_target_high;
        IOD_PROC_run=true;
        IOD_PROC_th  = std::thread(std::bind(&IOD_GPU_ThermalManager::IOD_PROC_thread,this));
        GPU_worker_run=true;
        GPU_worker_th = std::thread(std::bind(&IOD_GPU_ThermalManager::Thermal_worker_thread,this));
        IOD_PROCHOT=false;
        GPU_now_control=false;
    }
    void join()
    {
        GPU_worker_run = false;
        IOD_PROC_run = false;
        IOD_PROC_th.join();
        GPU_worker_th.join();
    }
    IOD_GPU_ThermalManager(const IOD_GPU_ThermalManager & other) = delete;
    // output control paramater
    std::atomic<bool> IOD_PROCHOT; // status flag
    std::atomic<bool> GPU_now_control; // status flag
    // output
    int GPU_now_maxCLK;
    int targetMhz_offset=0;
    //
    // performance profile
    unsigned int GPU_minCLK;
    unsigned int GPU_maxCLK;
    unsigned int MAX_TGP_mw;
    unsigned int MAX_Frequncy_Mhz;
    double IOD_high;
    double IOD_target_high;
    double IOD_low;

    void Thermal_worker_thread()
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
                    GPU_ifbuffer.write(record_id,tmp);
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
                cur_iod_TEMP/=GPU_ADJUST_INTERVAL_SEC;
                pre_iod_TEMP/=GPU_ADJUST_INTERVAL_SEC;
                
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
                    // printf("original gpu offset:%dMhz\n",targetMhz_offset);
                    if(targetMhz_offset>0) targetMhz_offset=0;
                }
                else
                {
                    IOD_Integer=0;
                    targetMhz_offset=static_cast<int>(50.0*(IOD_difference*1.0 - 6.0*IOD_derivative));
                    // printf("original gpu offset:%dMhz\n",targetMhz_offset);
                }
                if(std::abs(((double)targetMhz_offset)/((double)targetMHz))>0.2)
                {
                    targetMHz=static_cast<unsigned int>((targetMhz_offset>0)?(1.2*targetMHz):(0.8*targetMHz));
                    //printf("original gpu %d\n",targetMHz);
                }
                else
                {
                    targetMHz=targetMHz+targetMhz_offset;
                    //printf("original gpu %d\n",targetMHz);
                }
                if(targetMHz<GPU_minCLK)   targetMHz=GPU_minCLK;
                if(targetMHz>GPU_maxCLK)    targetMHz=GPU_maxCLK;

                //adjust algorithm end
                if((!IOD_PROCHOT) && GPU_now_control && GPU_worker_run)
                {
                    targetMHz = GPU_controller.GPUlgc(targetMHz,1,1);
                }
                GPU_now_maxCLK=targetMHz;
                //printf("set gpuclk to %d Mhz",targetMHz);
            }
        }
        return;
    }
    void IOD_PROC_thread()
    {
        while(IOD_PROC_run)
        {
            double cur_IOD_temp;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            cur_IOD_temp = IODfetcher.get_buffer_ptr()->get_Lastest_data();
            if(IODfetcher.get_data_invalid_status()!=0) continue;
            //update IOD_PROCHOT flag and GPU_now_control flag
            //reserve
            switch (IODfetcher.get_data_invalid_status())
            {
            case 1:
                /* code */
                break;
            case 2:
                
            default:
                break;
            }
            if(!IOD_PROCHOT)
            {
                if(cur_IOD_temp>=IOD_high)
                {
                    IOD_PROCHOT=true;
                    GPU_now_control=true;
                }
                else if(cur_IOD_temp<=IOD_low)
                {
                    IOD_PROCHOT=false;
                    if(GPU_now_control)
                    {
                        this->GPU_controller.GPUlgc(GPU_maxCLK,0,0);// reset "rgc"
                        GPU_now_control=false;
                    }
                }
                else
                {
                    IOD_PROCHOT=false;
                    GPU_now_control=true;
                }
            }
            else
            {
                if(cur_IOD_temp<IOD_high-5) IOD_PROCHOT=false;
            }

            if(IOD_PROCHOT)
            {
                this->GPU_controller.GPUlgc(GPU_minCLK,0,1);
            }
        }
    }
};

    
#endif    









#define NOMINMAX
#include <iostream>
#include<string>
#include<algorithm>
#include "nvml.h"
#include<thread>
#include<mutex>
#include<signal.h>
#include<vector>
#include<windows.h>
#include"hwisenssm2.h"
#define IOD_BUFFER_SIZE (30)
#define WORKER_BUFFER_SIZE (120)
#define PROC_NVIDIASMI_STR "nvidia-smi -lgc 0,900"
#define GPU_ADJUST_INTERVAL_SEC 3
#define MAX_TGP_mw 175000
#define MAX_Frequncy_Mhz 2580

// GPU control
int GPU_now_maxCLK=3000;
std::atomic<bool> IOD_PROCHOT;
std::atomic<bool> GPU_now_control;
const int GPU_maxCLK=3000;
const int GPU_minCLK=600;
//
//IOD_control
const double IOD_low=76;
const double IOD_high=87;
const double IOD_target_high=83.5;
//

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

struct GPU_Worker_info
{
    unsigned int GPU_Temp;
    unsigned int GPU_power;
    unsigned int GPU_clkMhz;
    nvmlUtilization_t GPU_Usage;
};
typedef struct GPU_Worker_info GPU_Worker_info;
template <typename T1,typename T2>
class CircularBuffer
{
private:
    std::vector<std::pair<T1,T2> >cbuffer;
    size_t writeIndex;
    size_t readIndex;
    size_t buffer_size;
    bool empty;
    std::mutex buffermutex;
public:
    CircularBuffer(size_t size): cbuffer(size),buffer_size(size),writeIndex(0),readIndex(0),empty(true){}
    int write(T1 wtime,T2 value)
    {
        buffermutex.lock();
        if(!empty && cbuffer[writeIndex].first==wtime)
        {
            buffermutex.unlock();
            return 1;
        }
        writeIndex++;
        if(empty)
        {
            empty=false;
            writeIndex=0;
        }
        if(writeIndex==buffer_size) writeIndex=0;
        cbuffer[writeIndex].second=value;
        cbuffer[writeIndex].first=wtime;
        buffermutex.unlock();
        return 0;
    }
    T2 read(T1 wtime)
    {
tagread:
        buffermutex.lock();
        readIndex=writeIndex;
        if(wtime>cbuffer[writeIndex].first)
        {
            buffermutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(main_Terminate) return cbuffer[writeIndex].second;
            goto tagread;
        }
        size_t tmp_count=0;
        while(1)
        {
            if(wtime==cbuffer[readIndex].first)
            {
                buffermutex.unlock();
                return cbuffer[readIndex].second;
            }
            else
            {
                if(cbuffer[readIndex].first>wtime && wtime>cbuffer[(readIndex==0)?(buffer_size-1):(readIndex-1)].first)
                {
                    std::cout<<"Warning:Lost middle data, that may be due to the hwinfo interval being too long"<<std::endl;
                    buffermutex.unlock();
                    return cbuffer[writeIndex].second;
                }
                readIndex=(readIndex==0)?(buffer_size-1):(readIndex-1);
                tmp_count++;
            }
           
            if(tmp_count>buffer_size)
            {
                std::cerr<<"ERROR:too long to lost old buffer"<<std::endl;
                // printf("required: %d but min is%d",wtime,cbuffer[(writeIndex+1)%buffer_size].first);
                std::cout<<"required: "<<wtime<<" but min is"<<cbuffer[(writeIndex+1)%buffer_size].first<<std::endl;
                buffermutex.unlock();
                return cbuffer[writeIndex].second;
            }
        }
    }
    T1 get_Lastest_ID()
    {
        return cbuffer[writeIndex].first;
    }
    
};
CircularBuffer<time_t,double>IOD_store(IOD_BUFFER_SIZE);


nvmlUnit_t Unit_id;
nvmlDevice_t Device_id;
//
std::mutex gpu_iflock;
unsigned int GPU_Temp=-1;
unsigned int GPU_power=-1;
unsigned int GPU_clock=-1;
nvmlUtilization_t GPU_Usage;
//
std::mutex iod_iflock;
double IOD_Temp;
volatile time_t IOD_time=0;
//
//
std::atomic<bool> isMonitored(false);
unsigned int max_wait=10;
std::atomic<bool> Watch_dog_run(false);
//

int NvError_count=0;

std::string str_info[4]={"Temp","Power_Usage","CLock","Ulti_Rates"};

struct hwi_iod
{
    int day;
    int month;
    int year;
    int hours;
    int mins;
    double second;
    double IOD_TEMP; 
};

std::mutex critical_func_lock_GPU;
void GPUlgc(int targetmax,int src,int behave) //behave 0 reset 1 set
{
    printf("tring to set to %dMhz from src %d\n",targetmax,src);
    if(targetmax<GPU_minCLK || targetmax>GPU_maxCLK)
    {
        std::cerr<<"detected error target GPU_frequnecy from src"<<src<<std::endl;
        targetmax=(GPU_minCLK+GPU_maxCLK)/2;
    }
    nvmlReturn_t nvmlflag3=NVML_SUCCESS;
    if(behave)  nvmlflag3=nvmlDeviceSetGpuLockedClocks (Device_id, 0, targetmax);
    else    nvmlDeviceResetGpuLockedClocks (Device_id);
    if(nvmlflag3!=NVML_SUCCESS)
    {
        std::cerr<<"set GPU LockedCLocks error "<<nvmlErrorString(nvmlflag3)<<std::endl;

        system(PROC_NVIDIASMI_STR);
    }
    //nvmlDeviceSetGpuLockedClocks ( nvmlDevice_t device, unsigned int  minGpuClockMHz, unsigned int  maxGpuClockMHz )
    //nvmlDeviceResetGpuLockedClocks ( nvmlDevice_t device )
}

void Watchdog()
{
    unsigned int cur_wait=0;
    while(Watch_dog_run)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if(isMonitored)
        {
            cur_wait=0;
            isMonitored=false;
        }
        else
        {
            cur_wait++;
        }
        if(cur_wait>max_wait)
        {
            std::cerr << "Watchdog detected too many misses, terminating process.\n";
            system(PROC_NVIDIASMI_STR);
            nvmlShutdown();  
            std::exit(EXIT_FAILURE);  
        }
    }
}

void NvErrorPrint(std::string str1,nvmlReturn_t nvflag)
{
    NvError_count++;
    std::cerr<<"get"+str1<<std::endl<<nvmlErrorString(nvflag)<<std::endl;
}

void get_GPU_status(nvmlDevice_t NvDevice)
{
    nvmlReturn_t nvml_flag1[10];
    gpu_iflock.lock();
    GPU_Temp=-1;    GPU_power=-1;   GPU_clock=-1;
    nvml_flag1[0] = nvmlDeviceGetTemperature(NvDevice,NVML_TEMPERATURE_GPU,&GPU_Temp);
    nvml_flag1[1] = nvmlDeviceGetPowerUsage(NvDevice,&GPU_power);
    nvml_flag1[2] = nvmlDeviceGetClock(NvDevice,NVML_CLOCK_GRAPHICS,NVML_CLOCK_ID_CURRENT,&GPU_clock);
    nvml_flag1[3] = nvmlDeviceGetUtilizationRates(NvDevice,&GPU_Usage);

    for(int i=0;i<=3;i++)   if(nvml_flag1[i]!=NVML_SUCCESS) NvErrorPrint(str_info[i],nvml_flag1[i]);

    gpu_iflock.unlock();
}



bool get_iod_temp_run=false;
void get_IOD_Temp()
{
    wchar_t W_hwissem[60];
    //wchar_t W_hwimutex[60];
    MultiByteToWideChar(CP_ACP, 0, HWiNFO_SENSORS_MAP_FILE_NAME2, -1, W_hwissem, 60);
    //MultiByteToWideChar(CP_ACP, 0, HWiNFO_SENSORS_SM2_MUTEX, -1, W_hwimutex, 60);
    HANDLE hHWiNFOMemory = OpenFileMappingW( FILE_MAP_READ, FALSE,W_hwissem );
    //HANDLE hHWIMutex = CreateMutexW(NULL, FALSE, W_hwimutex);
    DWORD dwIODReading;
    if (!hHWiNFOMemory) std::cerr<<"failed to open hwinfo shared memory";
    if (hHWiNFOMemory)
    {
        PHWiNFO_SENSORS_SHARED_MEM2 pHWiNFOMemory = (PHWiNFO_SENSORS_SHARED_MEM2) MapViewOfFile( hHWiNFOMemory, FILE_MAP_READ, 0, 0, 0 );
        // TODO: process signature, version, revision and poll time
        if(pHWiNFOMemory->dwSignature == 0x44414544 ) //=="DEAD"
        {
            //critical_erro
            std::cerr<<"hwi share memory inactive"<<std::endl;
        }
        if(pHWiNFOMemory->dwSignature != 0x53695748 ) //!="HWiS"
        {
            std::cerr<<"hwi share memory unknow content"<<std::endl;
            printf("hwi signature: %u\n",pHWiNFOMemory->dwSignature);
        }
        // loop through all available sensors
        // loop through all available readings
        for (DWORD dwReading = 0; dwReading < pHWiNFOMemory->dwNumReadingElements; dwReading++)
        {
            PHWiNFO_SENSORS_READING_ELEMENT reading = (PHWiNFO_SENSORS_READING_ELEMENT) ((BYTE*)pHWiNFOMemory + 
                pHWiNFOMemory->dwOffsetOfReadingSection + 
                (pHWiNFOMemory->dwSizeOfReadingElement * dwReading));
            if(strcmp(reading->szLabelOrig,"CPU IOD Hotspot")==0)
            {
                printf("iod found\n");
                dwIODReading=dwReading;
            }
        }
    }
    //CloseHandle(hHWiNFOMemory);
    PHWiNFO_SENSORS_SHARED_MEM2 pHWiNFOMemory;
    if(hHWiNFOMemory)    pHWiNFOMemory = (PHWiNFO_SENSORS_SHARED_MEM2) MapViewOfFile( hHWiNFOMemory, FILE_MAP_READ, 0, 0, 0 );
    while(get_iod_temp_run && hHWiNFOMemory)
    {
        PHWiNFO_SENSORS_READING_ELEMENT reading = (PHWiNFO_SENSORS_READING_ELEMENT) ((BYTE*)pHWiNFOMemory + 
        pHWiNFOMemory->dwOffsetOfReadingSection + 
        (pHWiNFOMemory->dwSizeOfReadingElement * dwIODReading));
        if(abs(pHWiNFOMemory->poll_time - std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))>3)
        {
            std::cerr<<"IOD last poll time too far"<<std::endl;
        }
        if(pHWiNFOMemory->dwSignature == 0x44414544 ) //=="DEAD"
        {
            //critical_erro
            std::cerr<<"hwi share memory inactive"<<std::endl;
        }
        IOD_store.write(pHWiNFOMemory->poll_time,reading->Value);
        iod_iflock.lock();
        IOD_Temp=reading->Value;
        IOD_time=pHWiNFOMemory->poll_time;
        isMonitored=true;
        iod_iflock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    CloseHandle(hHWiNFOMemory);
    //CloseHandle(hHWIMutex);
    return;
}

bool IOD_PROC_run=false;
void IOD_PROC()
{
    while(IOD_PROC_run)
    {
        double cur_IOD_temp;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        iod_iflock.lock();
        cur_IOD_temp=IOD_Temp;
        iod_iflock.unlock();
        if(!IOD_PROCHOT)
        {
            if(IOD_Temp>=IOD_high)
            {
                IOD_PROCHOT=true;
                GPU_now_control=true;
            }
            else if(IOD_Temp<=IOD_low)
            {
                IOD_PROCHOT=false;
                if(GPU_now_control)
                {
                    GPUlgc(GPU_maxCLK,0,0);//"rgc"
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
            if(IOD_Temp<IOD_high-5) IOD_PROCHOT=false;
        }
        if(IOD_PROCHOT)
        {
            critical_func_lock_GPU.lock();
            GPUlgc(GPU_minCLK,0,1);
            critical_func_lock_GPU.unlock();
        }
    }
}

void GPU_worker_info_get(long long & _record_id,CircularBuffer<long long,GPU_Worker_info> & _GPU_ifbuffer)
{
    GPU_Worker_info GPU_if;
    get_GPU_status(Device_id);
    gpu_iflock.lock();
    GPU_if.GPU_Temp=GPU_Temp;
    GPU_if.GPU_power=GPU_power;
    GPU_if.GPU_clkMhz=GPU_clock;
    GPU_if.GPU_Usage=GPU_Usage;
    gpu_iflock.unlock();
    _GPU_ifbuffer.write(++_record_id,GPU_if);
    std::this_thread::sleep_for(std::chrono::milliseconds(230));
}


bool GPU_worker_run=false;
void GPU_worker()
{
    while(GPU_worker_run)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(GPU_now_control)
        {
            CircularBuffer<long long,GPU_Worker_info> GPU_ifbuffer(WORKER_BUFFER_SIZE);
            CircularBuffer<long long,GPU_Worker_info> GPU_worker_culculate_buffer(WORKER_BUFFER_SIZE);
            long long record_id=0;
            long long culculate_id=0;
            unsigned int targetMHz=GPU_minCLK;

            time_t cur_time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            for(int i=0;i<4* GPU_ADJUST_INTERVAL_SEC ;i++)   GPU_worker_info_get(record_id,GPU_ifbuffer);

            culculate_id++;
            GPU_Worker_info GPU_if_avg;
            GPU_if_avg.GPU_clkMhz=0;
            GPU_if_avg.GPU_power=0;
            GPU_if_avg.GPU_Temp=0;
            GPU_if_avg.GPU_Usage.gpu=0;
            GPU_if_avg.GPU_Usage.memory=0;
            for(int i=0;i<4* GPU_ADJUST_INTERVAL_SEC;i++)
            {
                targetMHz = std::max( targetMHz,GPU_ifbuffer.read(record_id-i).GPU_clkMhz);
                GPU_if_avg.GPU_Temp+=GPU_ifbuffer.read(record_id-i).GPU_Temp;
                GPU_if_avg.GPU_clkMhz+=GPU_ifbuffer.read(record_id-i).GPU_clkMhz;
                GPU_if_avg.GPU_power+=GPU_ifbuffer.read(record_id-i).GPU_power;
                GPU_if_avg.GPU_Usage.gpu+=GPU_ifbuffer.read(record_id-i).GPU_Usage.gpu;
                GPU_if_avg.GPU_Usage.memory+=GPU_ifbuffer.read(record_id-i).GPU_Usage.memory;
            }
            GPU_if_avg.GPU_Temp/=(double)4*GPU_ADJUST_INTERVAL_SEC;
            GPU_if_avg.GPU_clkMhz/=(double)4*GPU_ADJUST_INTERVAL_SEC;
            GPU_if_avg.GPU_power/=(double)4*GPU_ADJUST_INTERVAL_SEC;
            GPU_if_avg.GPU_Usage.gpu/=(double)4*GPU_ADJUST_INTERVAL_SEC;
            GPU_if_avg.GPU_Usage.memory/=(double)4*GPU_ADJUST_INTERVAL_SEC;
            GPU_worker_culculate_buffer.write(culculate_id,GPU_if_avg);

            double IOD_Integer=0;
            while(GPU_now_control && GPU_worker_run)
            {
                for(int i=0;i<4*GPU_ADJUST_INTERVAL_SEC;i++)   GPU_worker_info_get(record_id,GPU_ifbuffer);

                culculate_id++;
                GPU_Worker_info GPU_if_avg;
                GPU_if_avg.GPU_clkMhz=0;
                GPU_if_avg.GPU_power=0;
                GPU_if_avg.GPU_Temp=0;
                GPU_if_avg.GPU_Usage.gpu=0;
                GPU_if_avg.GPU_Usage.memory=0;
                unsigned int rec_targetMHz=0;
                for(int i=0;i<4*GPU_ADJUST_INTERVAL_SEC;i++)
                {
                    rec_targetMHz=std::max(rec_targetMHz,GPU_ifbuffer.read(record_id-i).GPU_clkMhz);
                    GPU_if_avg.GPU_Temp+=GPU_ifbuffer.read(record_id-i).GPU_Temp;
                    GPU_if_avg.GPU_clkMhz+=GPU_ifbuffer.read(record_id-i).GPU_clkMhz;
                    GPU_if_avg.GPU_power+=GPU_ifbuffer.read(record_id-i).GPU_power;
                    GPU_if_avg.GPU_Usage.gpu+=GPU_ifbuffer.read(record_id-i).GPU_Usage.gpu;
                    GPU_if_avg.GPU_Usage.memory+=GPU_ifbuffer.read(record_id-i).GPU_Usage.memory;
                }
                GPU_if_avg.GPU_Temp/=(double)4*GPU_ADJUST_INTERVAL_SEC;
                GPU_if_avg.GPU_clkMhz/=(double)4*GPU_ADJUST_INTERVAL_SEC;
                GPU_if_avg.GPU_power/=(double)4*GPU_ADJUST_INTERVAL_SEC;
                GPU_if_avg.GPU_Usage.gpu/=(double)4*GPU_ADJUST_INTERVAL_SEC;
                GPU_if_avg.GPU_Usage.memory/=(double)4*GPU_ADJUST_INTERVAL_SEC;
                GPU_worker_culculate_buffer.write(culculate_id,GPU_if_avg);
                cur_time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

                double cur_iod_TEMP=0,pre_iod_TEMP=0;
                for(int i=0;i<GPU_ADJUST_INTERVAL_SEC;i++)
                {
                    cur_iod_TEMP+=IOD_store.read(cur_time-i);
                    pre_iod_TEMP+=IOD_store.read(cur_time-i-GPU_ADJUST_INTERVAL_SEC);
                }
                GPU_Worker_info cur_GPU_if_avg=GPU_worker_culculate_buffer.read(culculate_id);
                GPU_Worker_info pre_GPU_if_avg=GPU_worker_culculate_buffer.read(culculate_id-1);
                cur_iod_TEMP/=GPU_ADJUST_INTERVAL_SEC;pre_iod_TEMP/=GPU_ADJUST_INTERVAL_SEC;
                bool IOD_high = cur_iod_TEMP>IOD_target_high;
                double GPU_power_incresed = cur_GPU_if_avg.GPU_power - pre_GPU_if_avg.GPU_power;
                if(GPU_power_incresed>0) GPU_power_incresed=0;
                double IOD_derivative=cur_iod_TEMP-pre_iod_TEMP;
                double IOD_difference=IOD_target_high-cur_iod_TEMP;
                int targetMhz_offset=0;
                if(IOD_high||cur_GPU_if_avg.GPU_Usage.gpu>75)
                {
                    targetMHz=rec_targetMHz;
                }
                if(IOD_high)
                {
                    IOD_Integer+= (-IOD_difference);
                    targetMhz_offset=-75*((-IOD_difference)*1.0 + 4.0*IOD_derivative+(MAX_Frequncy_Mhz*GPU_power_incresed/MAX_TGP_mw)/60.0 + 0.5*IOD_Integer);
                    printf("original gpu offset:%dMhz\n",targetMhz_offset);
                    if(targetMhz_offset>0) targetMhz_offset=0;
                }
                else
                {
                    IOD_Integer=0;
                    targetMhz_offset=50.0*(IOD_difference*1.0 - 6.0*IOD_derivative);
                    printf("original gpu offset:%dMhz\n",targetMhz_offset);
                }
                if(std::abs(((double)targetMhz_offset)/((double)targetMHz))>0.2)
                {
                    targetMHz=(targetMhz_offset>0)?(1.2*targetMHz):(0.8*targetMHz);
                    //printf("original gpu %d\n",targetMHz);
                }
                else
                {
                    targetMHz=targetMHz+targetMhz_offset;
                    //printf("original gpu %d\n",targetMHz);
                }
                if(targetMHz<GPU_minCLK)   targetMHz=GPU_minCLK;
                if(targetMHz>GPU_maxCLK)    targetMHz=GPU_maxCLK;
                GPUlgc(targetMHz,1,1);
                GPU_now_maxCLK=targetMHz;
                //printf("set gpuclk to %d Mhz",targetMHz);
            }
        }
    }
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
    nvmlDeviceGetHandleByIndex_v2(0,&Device_id);

    get_iod_temp_run=true;
    std::thread IOD_thread(get_IOD_Temp);
    Watch_dog_run=true;
    std::thread watchdog_thread(Watchdog);
    IOD_PROC_run=true;
    std::thread IOD_protect_thread(IOD_PROC);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    GPU_worker_run=true;
    std::thread GPU_worker_thread(GPU_worker);
    while(1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        if(main_Terminate) break;
        bool ism=isMonitored,ism2=GPU_now_control;
        gpu_iflock.lock();
        iod_iflock.lock();
        time_t rawtime;  
        struct tm timeinfo;  
        time(&rawtime);  
        localtime_s(&timeinfo,&rawtime);      
        printf("%02d:%02d:%02d IOD_TEMP:%f GPU_POWER:%d CLK %d target:%d NOW_CONTROL: %d\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,IOD_Temp,GPU_power,GPU_clock,GPU_now_maxCLK,ism2);
        gpu_iflock.unlock();
        iod_iflock.unlock();
    }
    get_iod_temp_run=false;
    Watch_dog_run=false;
    IOD_PROC_run=false;
    GPU_worker_run=false;
    IOD_thread.join();
    watchdog_thread.join();
    IOD_protect_thread.join();
    GPU_worker_thread.join();
    //join
    system("nvidia-smi -rgc");
    nvmlShutdown();
    std::cout<<"GPU_MAX_CLOCK control has been Reset"<<std::endl;
    std::cout<<"terminate success";
    return 0;
}


//nvmlDeviceSetGpuLockedClocks ( nvmlDevice_t device, unsigned int  minGpuClockMHz, unsigned int  maxGpuClockMHz )
//nvmlDeviceResetGpuLockedClocks ( nvmlDevice_t device )

// void get_IOD_Temp()
// {
//     FILE * hwi_txt=fopen("HWiNFO_AlertLog.txt","r");
//     if(!hwi_txt)
//     {
//         std::cerr<<"failed to open HWiNFO_AlertLog"<<std::endl;
//         return;
//     }
//     char buffer1[204],buffer2[204];
//     while(get_iod_temp_run)
//     {
//         char* a1 = fgets(buffer1,200,hwi_txt);
//         char* a2;
//         if(a1==NULL)
//         {
//             std::this_thread::sleep_for(std::chrono::milliseconds(50));
//             continue;
//         }
// tag1 :
//         a2 = fgets(buffer2,200,hwi_txt);
//         if(a2!=NULL && buffer2[0]!='\n')
//         {
//             strcpy_s(buffer1,200,buffer2);
//             goto tag1;
//         }
//         struct hwi_iod now_iod;
//         int rv=tok_iod_info(buffer1,&now_iod);
//         if(rv==-2) continue;
//         iod_iflock.lock();
//         IOD_Temp=now_iod.IOD_TEMP;
//         auto cnow = std::chrono::system_clock::now();  
//         IOD_time = std::chrono::system_clock::to_time_t(cnow); 
//         isMonitored=true;
//         iod_iflock.unlock();
//         std::this_thread::sleep_for(std::chrono::milliseconds(900));
//     }
//     fclose(hwi_txt);
//     return;
// }

//int tok_iod_info(char * iod_str,struct hwi_iod * iod_a) //0 获取成功 //-2非IOD内容
// {
//     char * a5=strstr(iod_str,"CPU IOD Hotspot");
//     if(a5==nullptr) return -2;
//     a5+=16;
//     for(int i=0;i<10;i++)
//     {
//         if(*(a5+i)==32)
//         *(a5+i)=0;
//     }
//     sscanf(iod_str,"%d.%d.%d,%d:%d:%lf,CPU IOD Hotspot,%lf",&(iod_a->day),&(iod_a->month),&(iod_a->year)
//                                                 ,&(iod_a->hours),&(iod_a->mins),&(iod_a->second),&(iod_a->IOD_TEMP));
//     return 0;
// }
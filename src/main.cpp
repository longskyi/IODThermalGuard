#include <iostream>
#include "monitorUtils.h"
#include "thermalControl.h"
#include "iodUtils.h"
#include "P_watchdog.h"
#include "INIReader.h"
#include "Windows.h"
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

bool IsRunWithAdminPrivileges() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_ELEVATION elevation;
    DWORD dwSize;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
        CloseHandle(hToken);
        return elevation.TokenIsElevated != 0;
    }

    CloseHandle(hToken);
    return false;
}


bool RestartWithAdminPrivileges(const std::wstring& programPath) {
    SHELLEXECUTEINFOW sei = { sizeof(SHELLEXECUTEINFOW) };
    sei.lpVerb = L"runas";  
    sei.lpFile = programPath.c_str();
    sei.hwnd = NULL;
    sei.nShow = SW_SHOWNORMAL;

    LPWSTR cmdLine = GetCommandLineW();
    sei.lpParameters = cmdLine;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }

    exit(0);
    return true;
}


int main()
{   
    bool debug_mode = false;
    signal(SIGINT,SIGINT_Handler);
    SetConsoleOutputCP(65001);  
    SetConsoleCP(65001);
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    bool runWithAdmin = IsRunWithAdminPrivileges();
    if (!runWithAdmin && !debug_mode) {
        std::cout << "attempting to elevate privilege ..." << std::endl;
        
        if (!RestartWithAdminPrivileges(exePath)) {
            std::cout << "The program is running without administrator privilage, change of the gpu clk might not be valid." << std::endl;
        }
    }
    struct GPU_performance_settings gpu_s;
    struct IOD_performance_settings iod_s;
    
    read_settings(iod_s,gpu_s);

    nvmlReturn_t nvml_flag;
    unsigned int Unit_count=1;
    unsigned int Device_count;
    

    P_logger::init("IODThermalGuard.log",10);
    if(debug_mode)
        P_logger::glob_log_level = LogLevel::DEBUG;
    else
        P_logger::glob_log_level = LogLevel::INFO;
    
    P_logger::use_stdout = true;

    P_logger::addLog("hihi",LogLevel::INFO);
    
    nvml_flag=nvmlInit_v2();
    if(nvml_flag!=NVML_SUCCESS)
    {
        P_logger::addLog(nvmlErrorString(nvml_flag),LogLevel::ERRO);
        P_logger::close();
        system("pause");
        return 0;
    }
    
    nvmlDeviceGetCount_v2(&Device_count);
    if(Device_count <1)
    {
        P_logger::addLog("NVIDIA GPU device less than 1",LogLevel::ERRO);
        nvmlShutdown();
        P_logger::close();
        system("pause");
        return 0;
    }

    nvmlDevice_t gpu_Device;
    nvmlDeviceGetHandleByIndex_v2(0,&gpu_Device);


    auto iod_fetcher = HWINFOdataFetcher("CPU IOD Hotspot",P_logger::addLog);
    iod_fetcher.init();
    auto gpu_control = GPUController(gpu_Device,gpu_s,P_logger::addLog);
    auto thermalman = IOD_GPU_ThermalManager(gpu_control,iod_fetcher,gpu_s,iod_s,P_logger::addLog);
    P_watchdog::watchdog_start();
    
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
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
        bool ism3 = thermalman.IOD_PROCHOT;
        localtime_s(&timeinfo,&rawtime);

        GetConsoleScreenBufferInfo(hConsole, &csbi);
        SHORT x = csbi.dwCursorPosition.X;
        SHORT y = csbi.dwCursorPosition.Y;
        COORD line_head_coord;
        line_head_coord.X = 0;
        line_head_coord.Y = y;
        SetConsoleCursorPosition(hConsole, line_head_coord);
        printf("%02d:%02d:%02d IOD_TEMP:%f GPU_POWER:%dmW CLK %dMHz target:%dMHz CONTROL: %d,PROCHOT: %d",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,IOD_Temp,GPU_power,GPU_clock,GPU_now_maxCLK,ism2,ism3);
        fflush(stdout);
    }
    

    //join
    CircularBuffer_main_Terminate = true;
    P_watchdog::watchdog_join();
    iod_fetcher.join();
    thermalman.join();

    system("nvidia-smi -rgc");
    nvmlShutdown();
    std::cout<<"GPU_MAX_CLOCK control has been Reset"<<std::endl;
    std::cout<<"terminate success";
    system("pause");
    return 0;
}


// iod_s.IOD_high = 87;
// iod_s.IOD_low = 65;
// iod_s.IOD_target_high =70;
// gpu_s.GPU_maxCLK = 3000;
// gpu_s.GPU_minCLK = 600;
// gpu_s.MAX_Frequency_Mhz = 2580;
// gpu_s.MAX_TGP_mw = 175000;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <hwisenssm2.h>
#include <iostream>
#include <mutex>
#include <atomic>
#include "iodUtils.h"
#include "P_watchdog.h"
#include <format>
#include <string>
#include <functional>
#define WORKER_BUFFER_SIZE (64)


HWINFOdataFetcher::HWINFOdataFetcher(std::string _sensor_name)
{
    this->sensor_name = _sensor_name;
    this->sensor_data_buffer = std::make_shared<CircularBuffer<long long,double> >(WORKER_BUFFER_SIZE);
    this->logger = nullptr;
}

HWINFOdataFetcher::HWINFOdataFetcher(std::string _sensor_name , void (*_logger) (std::string logger_str,LogLevel level))
{
    this->sensor_name = _sensor_name;
    this->sensor_data_buffer = std::make_shared<CircularBuffer<long long,double> >(WORKER_BUFFER_SIZE);
    this->logger = _logger;
}

int HWINFOdataFetcher::init() // 0 for success , 1 for failed
{
    wchar_t W_hwissem[60];
    //wchar_t W_hwimutex[60];
    MultiByteToWideChar(CP_ACP, 0, HWiNFO_SENSORS_MAP_FILE_NAME2, -1, W_hwissem, 60);
    //MultiByteToWideChar(CP_ACP, 0, HWiNFO_SENSORS_SM2_MUTEX, -1, W_hwimutex, 60);
    hHWiNFOMemory = OpenFileMappingW( FILE_MAP_READ, FALSE,W_hwissem );
    //HANDLE hHWIMutex = CreateMutexW(NULL, FALSE, W_hwimutex);
    if (!hHWiNFOMemory)
    {
        if(logger)
            logger(std::string("unable to open HWINFO shared memory"),LogLevel::ERRO);
        return 1;
    }

    PHWiNFO_SENSORS_SHARED_MEM2 pHWiNFOMemory = (PHWiNFO_SENSORS_SHARED_MEM2) MapViewOfFile( hHWiNFOMemory, FILE_MAP_READ, 0, 0, 0 );
    // TODO: process signature, version, revision and poll time
    if(pHWiNFOMemory->dwSignature == 0x44414544 ) //=="DEAD"
    {
        if(logger)
            logger(std::string("IODfetcher init failed, hwi share memory inactive"),LogLevel::ERRO);
        return 1;
    }
    if(pHWiNFOMemory->dwSignature != 0x53695748 ) //!="HWiS"
    {
        if(logger)
        {
            logger(std::string("hwi share memory unknow content"),LogLevel::ERRO);
            logger(std::format("hwi signature: %u",pHWiNFOMemory->dwSignature),LogLevel::ERRO);
        }
            
        return 1;
    }
    // loop through all available sensors
    // loop through all available readings
    bool findiod=false;
    for (DWORD dwReading = 0; dwReading < pHWiNFOMemory->dwNumReadingElements; dwReading++)
    {
        PHWiNFO_SENSORS_READING_ELEMENT reading = (PHWiNFO_SENSORS_READING_ELEMENT) ((BYTE*)pHWiNFOMemory + 
            pHWiNFOMemory->dwOffsetOfReadingSection + 
            (pHWiNFOMemory->dwSizeOfReadingElement * dwReading));
        if(strcmp(reading->szLabelOrig,sensor_name.c_str())==0)
        {
            findiod = true;
            dwIODReading=dwReading;
            if(logger)
                logger(std::string(std::format("found HWINFO {} sensors",sensor_name)),LogLevel::INFO);   
            break;
        }
    }
    if(!findiod)
    {
        if(logger)
            logger(std::string(std::format("unable to find {} sensor",sensor_name)),LogLevel::ERRO);
        return 1;   
    }
    get_data_temp_run = true;
    HWIworker_thread = std::thread(std::bind(&HWINFOdataFetcher::worker_thread,this));
    P_watchdog::set(sensor_name);
    return 0;
}
void HWINFOdataFetcher::worker_thread()
{
    //CloseHandle(hHWiNFOMemory);
    PHWiNFO_SENSORS_SHARED_MEM2 pHWiNFOMemory;
    if(hHWiNFOMemory)    pHWiNFOMemory = (PHWiNFO_SENSORS_SHARED_MEM2) MapViewOfFile( hHWiNFOMemory, FILE_MAP_READ, 0, 0, 0 );
    while(get_data_temp_run && hHWiNFOMemory)
    {
        P_watchdog::feedWatchdog(sensor_name);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // TODO: process signature, version, revision and poll time
        PHWiNFO_SENSORS_READING_ELEMENT reading = (PHWiNFO_SENSORS_READING_ELEMENT) ((BYTE*)pHWiNFOMemory + 
        pHWiNFOMemory->dwOffsetOfReadingSection + 
        (pHWiNFOMemory->dwSizeOfReadingElement * dwIODReading));
        if(pHWiNFOMemory->dwSignature != 0x53695748 ) //=="DEAD"
        {
            if(data_invalid_status != 1)
            {
                logger("hwi share memory inactive",LogLevel::ERRO);
            }
            data_invalid_status = 1;
            continue;
        }
        if(abs(pHWiNFOMemory->poll_time - std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))>3)
        {
            if(data_invalid_status != 2)
            {
                logger("sensor last poll time too far",LogLevel::WARN);
            }
            data_invalid_status = 2;
            continue;
        }
        data_invalid_status = 0;
        sensor_data_buffer->write(pHWiNFOMemory->poll_time,reading->Value);
        sensor_value=reading->Value;
        sensor_time=pHWiNFOMemory->poll_time;
        //isMonitored=true;
    }
    CloseHandle(hHWiNFOMemory);
    //CloseHandle(hHWIMutex);
    return;
}
std::shared_ptr<CircularBuffer<long long,double>> HWINFOdataFetcher::get_buffer_ptr()
{
    return sensor_data_buffer;
}
int HWINFOdataFetcher::get_data_invalid_status()
{
    return data_invalid_status;
}
void HWINFOdataFetcher::join()
{
    get_data_temp_run = false;
    if(HWIworker_thread.joinable())
        HWIworker_thread.join();
    data_invalid_status = 1;
    P_watchdog::unset(sensor_name);
    return;
}    






#ifndef LOG_UTILS_CPP
#define LOG_UTILS_CPP
#include "logUtils.h"
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>

std::u8string getExecutablePath()
{
    wchar_t exePath[(MAX_PATH )+ 1];
    char8_t exePath_utf8[(MAX_PATH)*4+1];
    DWORD length = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if(length == 0)
    {
        throw std::runtime_error("Failed to get module file name");
    }
    int utf8_length = WideCharToMultiByte(CP_UTF8,0,exePath,-1,(char*)exePath_utf8,MAX_PATH*4,NULL,NULL);
    return std::u8string(exePath_utf8);
}


namespace P_logger
{
    LogLevel glob_log_level = LogLevel::WARN;
    std::string log_name="";
    std::vector<std::pair<time_point,std::string>> recent_log(10);
    std::ofstream log_fstream;
    bool use_stdout = false;
    unsigned int recent_log_buffer_size = 10 ;
    std::filesystem::path logFilePath;
    void init(std::string log_name , unsigned int _recent_log_buffer_size)
    {
        recent_log_buffer_size = _recent_log_buffer_size;
        recent_log.reserve(recent_log_buffer_size);
        std::u8string exePath = ::getExecutablePath();
        // 提取可执行文件所在的目录
        std::filesystem::path exeDirPath = std::filesystem::path(exePath).parent_path();
        
        // 检查文件名是否已经以 ".log" 结尾
        if(log_name.size()<1)
        {
            log_name = "default.log";
        }
        if (log_name.rfind(".log") != (log_name.size() - 4)) {
            log_name += ".log";
        }
        // 构建日志文件的路径
        logFilePath = exeDirPath / log_name;
        // 创建并打开日志文件
        log_fstream = std::ofstream (logFilePath , std::ios::out | std::ios::app);
        if (!log_fstream.is_open()) {
            log_name = "default.log";
            logFilePath = exeDirPath / log_name;
            log_fstream = std::ofstream (logFilePath , std::ios::out | std::ios::app);
            if(!log_fstream.is_open())
            {
                std::cerr<<"Unable to create log file , will only using stdout";
                use_stdout = true;
            }
        }
        glob_log_level = LogLevel::WARN;
    }
    void addLog(std::string log_str,LogLevel level)
    {
        if(level < glob_log_level)
            return;
        if(log_fstream.is_open())
        {
            log_fstream<<log_str<<std::endl;
        }
        if(use_stdout)
        {
            printf("\n%s\n",log_str.c_str());
        }
        if(recent_log.size()>=recent_log_buffer_size)
        {
            recent_log.erase(recent_log.begin());
        }
        auto q = std::make_pair(std::chrono::system_clock::now(),log_str);
        recent_log.push_back(q);
    }
    void close()
    {
        if(log_fstream.is_open())
        {
            log_fstream.close();
        }
        return;
    }
    void setLogLevel(LogLevel level)
    {
        glob_log_level = level;
    }
} // namespace P_logger


#endif
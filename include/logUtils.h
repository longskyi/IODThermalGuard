#ifndef LOG_UTILS_HEADER
#define LOG_UTILS_HEADER

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <string>
#include <filesystem>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
enum LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERRO,
    CRITICAL
};

std::u8string getExecutablePath();


namespace P_logger
{
    using time_point = std::chrono::system_clock::time_point;
    extern LogLevel glob_log_level;
    extern std::string log_name;
    extern std::vector<std::pair<time_point,std::string>> recent_log;
    extern std::ofstream log_fstream;
    extern unsigned int recent_log_buffer_size ;
    extern bool use_stdout;
    extern std::filesystem::path logFilePath;
    void init(std::string log_name="default.log" , unsigned int _recent_log_buffer_size=10);
    void addLog(std::string log_str,LogLevel level);
    void close();
    void setLogLevel(LogLevel level);
} // namespace P_logger


#endif
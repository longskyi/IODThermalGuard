
#ifndef _P_WATCH_DOG_HEADER 
#define _P_WATCH_DOG_HEADER 

#include <unordered_map>
#include <string>
#include <cstdint>

namespace P_watchdog
{
    extern std::unordered_map <std::string,uint8_t> watchdog_flag;
    int set(std::string _watchdog_flag_name); // return 0 for set success // return 1 for duplicate set
    int find(std::string _watchdog_flag_name); // return 1 for found , 0 for not found;
    void feedWatchdog(std::string _watchdog_flag_name);
    void unset(std::string _watchdog_flag_name);
    void watchdog_start();
    void watchdog_join();
} // namespace P_watchdog

#endif
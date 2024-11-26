#include <P_watchdog.h>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>
#include <iostream>

#define WATCHDOGTIMEOUT (10)

namespace P_watchdog
{
    static std::unordered_map <std::string,uint8_t> watchdog_flag_map;
    static std::mutex watchdog_mutex;
    static std::atomic<bool> watchdog_run;
    static std::thread watchdog_thread;
    int set(std::string _watchdog_flag_name) // return 0 for set success // return 1 for duplicate set and refresh
    {
        std::unique_lock<std::mutex> lock(watchdog_mutex);
        if( watchdog_flag_map.find(_watchdog_flag_name) == watchdog_flag_map.end())
        {
            watchdog_flag_map[_watchdog_flag_name] = 0;
            return 0;
        }
        else
        {
            watchdog_flag_map[_watchdog_flag_name] = 0;
            return 1;
        }
    }
    int find(std::string _watchdog_flag_name) // return 1 for found , 0 for not found;
    {
        std::unique_lock<std::mutex> lock(watchdog_mutex);
        if( watchdog_flag_map.find(_watchdog_flag_name) == watchdog_flag_map.end())
            return 0;
        else
            return 1;   

    }
    void refresh(std::string _watchdog_flag_name) // refresh watch dog flag
    {
        std::unique_lock<std::mutex> lock(watchdog_mutex);
        if( watchdog_flag_map.find(_watchdog_flag_name) == watchdog_flag_map.end())
            return;
        else
            watchdog_flag_map[_watchdog_flag_name]=0;   
    }
    void unset(std::string _watchdog_flag_name)
    {
        std::unique_lock<std::mutex> lock(watchdog_mutex);
        watchdog_flag_map.erase(_watchdog_flag_name);
    }
    void watchdog_worker_thread()
    {
        while(watchdog_run)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            watchdog_mutex.lock();
            for(auto & flag : watchdog_flag_map)
            {
                flag.second++;
                if(flag.second >= WATCHDOGTIMEOUT)
                {
                    std::cerr << "Watchdog detected too many misses, terminating process.\n";
                    std::cerr << "Watchdog detected" << flag.first <<" caught many misses\n";
                    watchdog_mutex.unlock();
                    std::exit(EXIT_FAILURE);  
                }
            }
            watchdog_mutex.unlock();
        }
    }
    void watchdog_start()
    {
        watchdog_run = true;
        watchdog_thread = std::thread(watchdog_worker_thread);
        return;
    }
    void watchdog_join()
    {
        watchdog_run = false;
        watchdog_thread.join();
        watchdog_flag_map.clear();
        return;
    }
} // namespace P_watchdog






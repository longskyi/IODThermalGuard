#ifndef CIRCULARBUFFER_HEADER
#define CIRCULARBUFFER_HEADER
#include <vector>
#include <mutex>
#include <chrono>

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
    bool main_Terminate;
    CircularBuffer(size_t size): cbuffer(size),buffer_size(size),writeIndex(0),readIndex(0),empty(true),main_Terminate(false){}
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
            if(main_Terminate)
                return cbuffer[writeIndex].second;
            else
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
    T2 get_Lastest_data()
    {
        return cbuffer[writeIndex].second;
    }
    
};

#endif
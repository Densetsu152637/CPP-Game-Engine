//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_THREADPOOL_H
#define CPP_GAME_ENGINE_THREADPOOL_H
#include <thread>

#include "../structs/arraylist.h"
#include "../structs/smartstring.h"

class Threadpool;

struct Thread
{
    std::thread thread;
    Threadpool* pool;

};

void thread_global_entrance_point(Thread* thread);

class Threadpool {

    ArrayList<Thread> m_pool;
    SmartString m_name;


};



#endif //CPP_GAME_ENGINE_THREADPOOL_H

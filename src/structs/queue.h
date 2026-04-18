//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_QUEUE_H
#define CPP_GAME_ENGINE_QUEUE_H
#include "arraylist.h"

template <typename T>
class Queue {

    Array<T> m_queue;
    int m_start = 0;
    int m_end = 0;

    void _resize(size_t new_size);
    void _resize_if_necessary();

public:

    Queue() : Queue(ARRAY_DEFAULT_INITIAL_CAPACITY) {}
    Queue(size_t initial_cap)
    {
        _resize(initial_cap);
    }
    ~Queue()
    {
        if (nullptr != m_queue.ptr) delete[] m_queue.ptr;
    }

    void append(T item);
    T pop();

};



#endif //CPP_GAME_ENGINE_QUEUE_H

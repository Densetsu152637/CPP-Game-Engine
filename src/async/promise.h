//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_PROMISE_H
#define CPP_GAME_ENGINE_PROMISE_H

#include <mutex>

#include "../functional/functions.h"
#include "../functional/result.h"
#include "../async/threadpool.h"
#include "../structs/arraylist.h"
#include "lock/waiter.h"

template <typename T>
class Promise {

    const Threadpool* m_pool;
    const ArrayList<Consumer<T>*> listeners;

    Result<T> m_result;
    Waiter m_wait;
    bool m_done;

    void _attach(Consumer<T>* p);

public:

    Promise() {}
    ~Promise() = default;

    void complete(Result<T> result);
    Result<T>& await();

    template <typename U> Promise<U> then(Function<T, U>&& f);
    Promise<T> catch_error(Function<T, std::exception&>&& f);

};



#endif //CPP_GAME_ENGINE_PROMISE_H

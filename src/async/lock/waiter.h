//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_WAITER_H
#define CPP_GAME_ENGINE_WAITER_H

class Waiter
{

    bool signaled = false;


public:

    void reset_signal();
    void signal();
    void await();

};

#endif //CPP_GAME_ENGINE_WAITER_H

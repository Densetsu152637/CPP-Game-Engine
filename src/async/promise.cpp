//
// Created by Nicholas on 18/04/26.
//

#include "promise.h"

template <typename T>
void Promise<T>::complete(Result<T> result)
{

}

template <typename T>
Result<T>& Promise<T>::await()
{
    m_wait.await();
    return this->m_result;
}

template <typename T>
template <typename U>
Promise<U> Promise<T>::then(Function<T, U>&& f)
{

}

template <typename T>
Promise<T> Promise<T>::catch_error(Function<T, std::exception&>&& f)
{

}

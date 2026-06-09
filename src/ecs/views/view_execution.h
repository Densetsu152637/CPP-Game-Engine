//
// View iteration chunking and async wait helpers.
//

#pragma once

#include <algorithm>
#include <exception>

#include "../../async/threadpool.h"

namespace ecs_view_detail
{
    struct DenseRange
    {
        size_t begin = 0;
        size_t end = 0;
    };

    inline size_t chunk_count(const size_t total, const size_t chunkSize)
    {
        return (total + chunkSize - 1) / chunkSize;
    }

    inline DenseRange make_chunk_range(const size_t chunk, const size_t chunkSize, const size_t total)
    {
        const size_t begin = chunk * chunkSize;
        return { begin, std::min(total, begin + chunkSize) };
    }

    inline size_t dense_chunk_size(const size_t total, Threadpool& pool, const size_t minChunk)
    {
        const size_t numThreads = std::max<size_t>(1, pool.size());
        const size_t defaultChunk = std::max<size_t>(1, total / numThreads);
        return std::max<size_t>(
            1,
            minChunk == 0 ? defaultChunk : minChunk
        );
    }

    template <typename T>
    void await_all(ArrayList<Promise<T>>& promises)
    {
        for (auto& promise : promises)
        {
            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
    }
}

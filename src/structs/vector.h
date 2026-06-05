//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <cfloat>
#include <climits>
#include <cstddef>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201) // nameless struct/union (used for direct component access)
#endif

#if defined(__SSE2__) || defined(__SSE4_1__) || defined(__AVX__) || defined(__AVX2__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <immintrin.h>
#endif

#if defined(__AVX2__)
#define CPP_GAME_ENGINE_HAS_AVX2 1
#else
#define CPP_GAME_ENGINE_HAS_AVX2 0
#endif

#if defined(__AVX__) || defined(__AVX2__)
#define CPP_GAME_ENGINE_HAS_AVX 1
#else
#define CPP_GAME_ENGINE_HAS_AVX 0
#endif

#if defined(__SSE4_1__)
#define CPP_GAME_ENGINE_HAS_SSE41 1
#else
#define CPP_GAME_ENGINE_HAS_SSE41 0
#endif

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define CPP_GAME_ENGINE_HAS_SSE 1
#else
#define CPP_GAME_ENGINE_HAS_SSE 0
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define CPP_GAME_ENGINE_HAS_SSE2 1
#else
#define CPP_GAME_ENGINE_HAS_SSE2 0
#endif

namespace vector_detail
{
    template<typename T, std::size_t N>
    inline void scalar_add(T (&dest)[N], const T (&lhs)[N], const T (&rhs)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] + rhs[i];
        }
    }

    template<typename T, std::size_t N>
    inline void scalar_sub(T (&dest)[N], const T (&lhs)[N], const T (&rhs)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] - rhs[i];
        }
    }

    template<typename T, std::size_t N>
    inline void scalar_mul(T (&dest)[N], const T (&lhs)[N], const T (&rhs)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] * rhs[i];
        }
    }

    template<typename T, std::size_t N>
    inline void scalar_div(T (&dest)[N], const T (&lhs)[N], const T (&rhs)[N], T zero_division_value)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = rhs[i] == static_cast<T>(0) ? zero_division_value : lhs[i] / rhs[i];
        }
    }

    template<typename T, std::size_t N>
    inline void div(T (&dest)[N], const T (&lhs)[N], const T (&rhs)[N], T zero_division_value)
    {
        scalar_div(dest, lhs, rhs, zero_division_value);
    }

    template<typename T, std::size_t N>
    inline void fill(T (&dest)[N], T value)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = value;
        }
    }

    template<typename T, std::size_t N>
    inline void add_scalar(T (&dest)[N], const T (&lhs)[N], T rhs)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] + rhs;
        }
    }

    template<typename T, std::size_t N>
    inline void sub_scalar(T (&dest)[N], const T (&lhs)[N], T rhs)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] - rhs;
        }
    }

    template<typename T, std::size_t N>
    inline void scalar_sub_from(T (&dest)[N], T lhs, const T (&rhs)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs - rhs[i];
        }
    }

    template<typename T, std::size_t N>
    inline void mul_scalar(T (&dest)[N], const T (&lhs)[N], T rhs)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] * rhs;
        }
    }

    template<typename T, std::size_t N>
    inline void div_scalar(T (&dest)[N], const T (&lhs)[N], T rhs, T zero_division_value)
    {
        if (rhs == static_cast<T>(0))
        {
            fill(dest, zero_division_value);
            return;
        }

        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = lhs[i] / rhs;
        }
    }

    template<typename T, std::size_t N>
    inline void scalar_div_by_array(T (&dest)[N], T lhs, const T (&rhs)[N], T zero_division_value)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            dest[i] = rhs[i] == static_cast<T>(0) ? zero_division_value : lhs / rhs[i];
        }
    }

    inline void add(float (&dest)[4], const float (&lhs)[4], const float (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_loadu_ps(rhs);
        _mm_storeu_ps(dest, _mm_add_ps(left, right));
#else
        scalar_add(dest, lhs, rhs);
#endif
    }

    inline void sub(float (&dest)[4], const float (&lhs)[4], const float (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_loadu_ps(rhs);
        _mm_storeu_ps(dest, _mm_sub_ps(left, right));
#else
        scalar_sub(dest, lhs, rhs);
#endif
    }

    inline void mul(float (&dest)[4], const float (&lhs)[4], const float (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_loadu_ps(rhs);
        _mm_storeu_ps(dest, _mm_mul_ps(left, right));
#else
        scalar_mul(dest, lhs, rhs);
#endif
    }

    inline void add_scalar(float (&dest)[4], const float (&lhs)[4], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_set1_ps(rhs);
        _mm_storeu_ps(dest, _mm_add_ps(left, right));
#else
        add_scalar<float, 4>(dest, lhs, rhs);
#endif
    }

    inline void sub_scalar(float (&dest)[4], const float (&lhs)[4], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_set1_ps(rhs);
        _mm_storeu_ps(dest, _mm_sub_ps(left, right));
#else
        sub_scalar<float, 4>(dest, lhs, rhs);
#endif
    }

    inline void scalar_sub_from(float (&dest)[4], float lhs, const float (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_set1_ps(lhs);
        const __m128 right = _mm_loadu_ps(rhs);
        _mm_storeu_ps(dest, _mm_sub_ps(left, right));
#else
        scalar_sub_from<float, 4>(dest, lhs, rhs);
#endif
    }

    inline void mul_scalar(float (&dest)[4], const float (&lhs)[4], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_set1_ps(rhs);
        _mm_storeu_ps(dest, _mm_mul_ps(left, right));
#else
        mul_scalar<float, 4>(dest, lhs, rhs);
#endif
    }

    inline void div_scalar(float (&dest)[4], const float (&lhs)[4], float rhs, float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        if (rhs == 0.0f)
        {
            fill(dest, zero_division_value);
            return;
        }

        const __m128 left = _mm_loadu_ps(lhs);
        const __m128 right = _mm_set1_ps(rhs);
        _mm_storeu_ps(dest, _mm_div_ps(left, right));
#else
        div_scalar<float, 4>(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void scalar_div_by_array(float (&dest)[4], float lhs, const float (&rhs)[4], float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        alignas(16) float safe_rhs[4];
        for (std::size_t i = 0; i < 4; ++i)
        {
            safe_rhs[i] = rhs[i] == 0.0f ? 1.0f : rhs[i];
        }

        const __m128 left = _mm_set1_ps(lhs);
        const __m128 right = _mm_load_ps(safe_rhs);
        __m128 result = _mm_div_ps(left, right);
        alignas(16) float temp[4];
        _mm_store_ps(temp, result);

        for (std::size_t i = 0; i < 4; ++i)
        {
            dest[i] = rhs[i] == 0.0f ? zero_division_value : temp[i];
        }
#else
        scalar_div_by_array<float, 4>(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void div(float (&dest)[4], const float (&lhs)[4], const float (&rhs)[4], float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_SSE
        alignas(16) float safe_rhs[4];
        alignas(16) float safe_lhs[4];
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (rhs[i] == 0.0f)
            {
                safe_rhs[i] = 1.0f;
                safe_lhs[i] = zero_division_value;
            }
            else
            {
                safe_rhs[i] = rhs[i];
                safe_lhs[i] = lhs[i];
            }
        }

        const __m128 left = _mm_load_ps(safe_lhs);
        const __m128 right = _mm_load_ps(safe_rhs);
        _mm_storeu_ps(dest, _mm_div_ps(left, right));
#else
        scalar_div(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void add(float (&dest)[8], const float (&lhs)[8], const float (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_loadu_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_add_ps(left, right));
#else
        scalar_add(dest, lhs, rhs);
#endif
    }

    inline void sub(float (&dest)[8], const float (&lhs)[8], const float (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_loadu_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_sub_ps(left, right));
#else
        scalar_sub(dest, lhs, rhs);
#endif
    }

    inline void mul(float (&dest)[8], const float (&lhs)[8], const float (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_loadu_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_mul_ps(left, right));
#else
        scalar_mul(dest, lhs, rhs);
#endif
    }

    inline void add_scalar(float (&dest)[8], const float (&lhs)[8], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_set1_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_add_ps(left, right));
#else
        add_scalar<float, 8>(dest, lhs, rhs);
#endif
    }

    inline void sub_scalar(float (&dest)[8], const float (&lhs)[8], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_set1_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_sub_ps(left, right));
#else
        sub_scalar<float, 8>(dest, lhs, rhs);
#endif
    }

    inline void scalar_sub_from(float (&dest)[8], float lhs, const float (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_set1_ps(lhs);
        const __m256 right = _mm256_loadu_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_sub_ps(left, right));
#else
        scalar_sub_from<float, 8>(dest, lhs, rhs);
#endif
    }

    inline void mul_scalar(float (&dest)[8], const float (&lhs)[8], float rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_set1_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_mul_ps(left, right));
#else
        mul_scalar<float, 8>(dest, lhs, rhs);
#endif
    }

    inline void div_scalar(float (&dest)[8], const float (&lhs)[8], float rhs, float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        if (rhs == 0.0f)
        {
            fill(dest, zero_division_value);
            return;
        }

        const __m256 left = _mm256_loadu_ps(lhs);
        const __m256 right = _mm256_set1_ps(rhs);
        _mm256_storeu_ps(dest, _mm256_div_ps(left, right));
#else
        div_scalar<float, 8>(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void scalar_div_by_array(float (&dest)[8], float lhs, const float (&rhs)[8], float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        alignas(32) float safe_rhs[8];
        for (std::size_t i = 0; i < 8; ++i)
        {
            safe_rhs[i] = rhs[i] == 0.0f ? 1.0f : rhs[i];
        }

        const __m256 left = _mm256_set1_ps(lhs);
        const __m256 right = _mm256_load_ps(safe_rhs);
        __m256 result = _mm256_div_ps(left, right);
        alignas(32) float temp[8];
        _mm256_store_ps(temp, result);

        for (std::size_t i = 0; i < 8; ++i)
        {
            dest[i] = rhs[i] == 0.0f ? zero_division_value : temp[i];
        }
#else
        scalar_div_by_array<float, 8>(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void div(float (&dest)[8], const float (&lhs)[8], const float (&rhs)[8], float zero_division_value)
    {
#if CPP_GAME_ENGINE_HAS_AVX
        alignas(32) float safe_rhs[8];
        alignas(32) float safe_lhs[8];
        for (std::size_t i = 0; i < 8; ++i)
        {
            if (rhs[i] == 0.0f)
            {
                safe_rhs[i] = 1.0f;
                safe_lhs[i] = zero_division_value;
            }
            else
            {
                safe_rhs[i] = rhs[i];
                safe_lhs[i] = lhs[i];
            }
        }

        const __m256 left = _mm256_load_ps(safe_lhs);
        const __m256 right = _mm256_load_ps(safe_rhs);
        _mm256_storeu_ps(dest, _mm256_div_ps(left, right));
#else
        scalar_div(dest, lhs, rhs, zero_division_value);
#endif
    }

    inline void add(int (&dest)[4], const int (&lhs)[4], const int (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE2
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_add_epi32(left, right));
#else
        scalar_add(dest, lhs, rhs);
#endif
    }

    inline void sub(int (&dest)[4], const int (&lhs)[4], const int (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE2
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_sub_epi32(left, right));
#else
        scalar_sub(dest, lhs, rhs);
#endif
    }

    inline void mul(int (&dest)[4], const int (&lhs)[4], const int (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE41
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_mullo_epi32(left, right));
#else
        scalar_mul(dest, lhs, rhs);
#endif
    }

    inline void add_scalar(int (&dest)[4], const int (&lhs)[4], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE2
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_set1_epi32(rhs);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_add_epi32(left, right));
#else
        add_scalar<int, 4>(dest, lhs, rhs);
#endif
    }

    inline void sub_scalar(int (&dest)[4], const int (&lhs)[4], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE2
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_set1_epi32(rhs);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_sub_epi32(left, right));
#else
        sub_scalar<int, 4>(dest, lhs, rhs);
#endif
    }

    inline void scalar_sub_from(int (&dest)[4], int lhs, const int (&rhs)[4])
    {
#if CPP_GAME_ENGINE_HAS_SSE2
        const __m128i left = _mm_set1_epi32(lhs);
        const __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rhs));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_sub_epi32(left, right));
#else
        scalar_sub_from<int, 4>(dest, lhs, rhs);
#endif
    }

    inline void mul_scalar(int (&dest)[4], const int (&lhs)[4], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_SSE41
        const __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(lhs));
        const __m128i right = _mm_set1_epi32(rhs);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_mullo_epi32(left, right));
#else
        mul_scalar<int, 4>(dest, lhs, rhs);
#endif
    }

    inline void add(int (&dest)[8], const int (&lhs)[8], const int (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_add_epi32(left, right));
#else
        scalar_add(dest, lhs, rhs);
#endif
    }

    inline void sub(int (&dest)[8], const int (&lhs)[8], const int (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_sub_epi32(left, right));
#else
        scalar_sub(dest, lhs, rhs);
#endif
    }

    inline void mul(int (&dest)[8], const int (&lhs)[8], const int (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_mullo_epi32(left, right));
#else
        scalar_mul(dest, lhs, rhs);
#endif
    }

    inline void add_scalar(int (&dest)[8], const int (&lhs)[8], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_set1_epi32(rhs);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_add_epi32(left, right));
#else
        add_scalar<int, 8>(dest, lhs, rhs);
#endif
    }

    inline void sub_scalar(int (&dest)[8], const int (&lhs)[8], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_set1_epi32(rhs);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_sub_epi32(left, right));
#else
        sub_scalar<int, 8>(dest, lhs, rhs);
#endif
    }

    inline void scalar_sub_from(int (&dest)[8], int lhs, const int (&rhs)[8])
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_set1_epi32(lhs);
        const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_sub_epi32(left, right));
#else
        scalar_sub_from<int, 8>(dest, lhs, rhs);
#endif
    }

    inline void mul_scalar(int (&dest)[8], const int (&lhs)[8], int rhs)
    {
#if CPP_GAME_ENGINE_HAS_AVX2
        const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
        const __m256i right = _mm256_set1_epi32(rhs);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_mullo_epi32(left, right));
#else
        mul_scalar<int, 8>(dest, lhs, rhs);
#endif
    }

    inline void add(int (&dest)[2], const int (&lhs)[2], const int (&rhs)[2])
    {
        scalar_add(dest, lhs, rhs);
    }

    inline void sub(int (&dest)[2], const int (&lhs)[2], const int (&rhs)[2])
    {
        scalar_sub(dest, lhs, rhs);
    }

    inline void mul(int (&dest)[2], const int (&lhs)[2], const int (&rhs)[2])
    {
        scalar_mul(dest, lhs, rhs);
    }

    inline void add(int (&dest)[3], const int (&lhs)[3], const int (&rhs)[3])
    {
        scalar_add(dest, lhs, rhs);
    }

    inline void sub(int (&dest)[3], const int (&lhs)[3], const int (&rhs)[3])
    {
        scalar_sub(dest, lhs, rhs);
    }

    inline void mul(int (&dest)[3], const int (&lhs)[3], const int (&rhs)[3])
    {
        scalar_mul(dest, lhs, rhs);
    }

    inline void add(float (&dest)[2], const float (&lhs)[2], const float (&rhs)[2])
    {
        scalar_add(dest, lhs, rhs);
    }

    inline void sub(float (&dest)[2], const float (&lhs)[2], const float (&rhs)[2])
    {
        scalar_sub(dest, lhs, rhs);
    }

    inline void mul(float (&dest)[2], const float (&lhs)[2], const float (&rhs)[2])
    {
        scalar_mul(dest, lhs, rhs);
    }

    inline void add(float (&dest)[3], const float (&lhs)[3], const float (&rhs)[3])
    {
        scalar_add(dest, lhs, rhs);
    }

    inline void sub(float (&dest)[3], const float (&lhs)[3], const float (&rhs)[3])
    {
        scalar_sub(dest, lhs, rhs);
    }

    inline void mul(float (&dest)[3], const float (&lhs)[3], const float (&rhs)[3])
    {
        scalar_mul(dest, lhs, rhs);
    }

    inline void add(float (&dest)[16], const float (&lhs)[16], const float (&rhs)[16])
    {
        add(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), reinterpret_cast<const float (&)[4]>(rhs[0]));
        add(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), reinterpret_cast<const float (&)[4]>(rhs[4]));
        add(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), reinterpret_cast<const float (&)[4]>(rhs[8]));
        add(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), reinterpret_cast<const float (&)[4]>(rhs[12]));
    }

    inline void sub(float (&dest)[16], const float (&lhs)[16], const float (&rhs)[16])
    {
        sub(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), reinterpret_cast<const float (&)[4]>(rhs[0]));
        sub(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), reinterpret_cast<const float (&)[4]>(rhs[4]));
        sub(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), reinterpret_cast<const float (&)[4]>(rhs[8]));
        sub(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), reinterpret_cast<const float (&)[4]>(rhs[12]));
    }

    inline void mul(float (&dest)[16], const float (&lhs)[16], const float (&rhs)[16])
    {
        mul(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), reinterpret_cast<const float (&)[4]>(rhs[0]));
        mul(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), reinterpret_cast<const float (&)[4]>(rhs[4]));
        mul(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), reinterpret_cast<const float (&)[4]>(rhs[8]));
        mul(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), reinterpret_cast<const float (&)[4]>(rhs[12]));
    }

    inline void div(float (&dest)[16], const float (&lhs)[16], const float (&rhs)[16], float zero_division_value)
    {
        div(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), reinterpret_cast<const float (&)[4]>(rhs[0]), zero_division_value);
        div(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), reinterpret_cast<const float (&)[4]>(rhs[4]), zero_division_value);
        div(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), reinterpret_cast<const float (&)[4]>(rhs[8]), zero_division_value);
        div(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), reinterpret_cast<const float (&)[4]>(rhs[12]), zero_division_value);
    }

    inline void add_scalar(float (&dest)[16], const float (&lhs)[16], float rhs)
    {
        add_scalar(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), rhs);
        add_scalar(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), rhs);
        add_scalar(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), rhs);
        add_scalar(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), rhs);
    }

    inline void sub_scalar(float (&dest)[16], const float (&lhs)[16], float rhs)
    {
        sub_scalar(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), rhs);
        sub_scalar(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), rhs);
        sub_scalar(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), rhs);
        sub_scalar(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), rhs);
    }

    inline void scalar_sub_from(float (&dest)[16], float lhs, const float (&rhs)[16])
    {
        scalar_sub_from(reinterpret_cast<float (&)[4]>(dest[0]), lhs, reinterpret_cast<const float (&)[4]>(rhs[0]));
        scalar_sub_from(reinterpret_cast<float (&)[4]>(dest[4]), lhs, reinterpret_cast<const float (&)[4]>(rhs[4]));
        scalar_sub_from(reinterpret_cast<float (&)[4]>(dest[8]), lhs, reinterpret_cast<const float (&)[4]>(rhs[8]));
        scalar_sub_from(reinterpret_cast<float (&)[4]>(dest[12]), lhs, reinterpret_cast<const float (&)[4]>(rhs[12]));
    }

    inline void mul_scalar(float (&dest)[16], const float (&lhs)[16], float rhs)
    {
        mul_scalar(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), rhs);
        mul_scalar(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), rhs);
        mul_scalar(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), rhs);
        mul_scalar(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), rhs);
    }

    inline void div_scalar(float (&dest)[16], const float (&lhs)[16], float rhs, float zero_division_value)
    {
        div_scalar(reinterpret_cast<float (&)[4]>(dest[0]), reinterpret_cast<const float (&)[4]>(lhs[0]), rhs, zero_division_value);
        div_scalar(reinterpret_cast<float (&)[4]>(dest[4]), reinterpret_cast<const float (&)[4]>(lhs[4]), rhs, zero_division_value);
        div_scalar(reinterpret_cast<float (&)[4]>(dest[8]), reinterpret_cast<const float (&)[4]>(lhs[8]), rhs, zero_division_value);
        div_scalar(reinterpret_cast<float (&)[4]>(dest[12]), reinterpret_cast<const float (&)[4]>(lhs[12]), rhs, zero_division_value);
    }

    inline void scalar_div_by_array(float (&dest)[16], float lhs, const float (&rhs)[16], float zero_division_value)
    {
        scalar_div_by_array(reinterpret_cast<float (&)[4]>(dest[0]), lhs, reinterpret_cast<const float (&)[4]>(rhs[0]), zero_division_value);
        scalar_div_by_array(reinterpret_cast<float (&)[4]>(dest[4]), lhs, reinterpret_cast<const float (&)[4]>(rhs[4]), zero_division_value);
        scalar_div_by_array(reinterpret_cast<float (&)[4]>(dest[8]), lhs, reinterpret_cast<const float (&)[4]>(rhs[8]), zero_division_value);
        scalar_div_by_array(reinterpret_cast<float (&)[4]>(dest[12]), lhs, reinterpret_cast<const float (&)[4]>(rhs[12]), zero_division_value);
    }
}

#define CPP_GAME_ENGINE_ARRAY_METHODS(TYPE, ELEMENT_TYPE, ZERO_DIVISION_VALUE) \
    inline TYPE operator+(const TYPE& other) const \
    { \
        TYPE dest{}; \
        vector_detail::add(dest.arr, this->arr, other.arr); \
        return dest; \
    } \
    inline TYPE operator-(const TYPE& other) const \
    { \
        TYPE dest{}; \
        vector_detail::sub(dest.arr, this->arr, other.arr); \
        return dest; \
    } \
    inline TYPE operator*(const TYPE& other) const \
    { \
        TYPE dest{}; \
        vector_detail::mul(dest.arr, this->arr, other.arr); \
        return dest; \
    } \
    inline TYPE operator/(const TYPE& other) const \
    { \
        TYPE dest{}; \
        vector_detail::div(dest.arr, this->arr, other.arr, static_cast<ELEMENT_TYPE>(ZERO_DIVISION_VALUE)); \
        return dest; \
    } \
    inline TYPE& operator+=(const TYPE& other) \
    { \
        vector_detail::add(this->arr, this->arr, other.arr); \
        return *this; \
    } \
    inline TYPE& operator-=(const TYPE& other) \
    { \
        vector_detail::sub(this->arr, this->arr, other.arr); \
        return *this; \
    } \
    inline TYPE& operator*=(const TYPE& other) \
    { \
        vector_detail::mul(this->arr, this->arr, other.arr); \
        return *this; \
    } \
    inline TYPE& operator/=(const TYPE& other) \
    { \
        vector_detail::div(this->arr, this->arr, other.arr, static_cast<ELEMENT_TYPE>(ZERO_DIVISION_VALUE)); \
        return *this; \
    } \
    inline TYPE operator+(ELEMENT_TYPE scalar) const \
    { \
        TYPE dest{}; \
        vector_detail::add_scalar(dest.arr, this->arr, scalar); \
        return dest; \
    } \
    inline TYPE operator-(ELEMENT_TYPE scalar) const \
    { \
        TYPE dest{}; \
        vector_detail::sub_scalar(dest.arr, this->arr, scalar); \
        return dest; \
    } \
    inline TYPE operator*(ELEMENT_TYPE scalar) const \
    { \
        TYPE dest{}; \
        vector_detail::mul_scalar(dest.arr, this->arr, scalar); \
        return dest; \
    } \
    inline TYPE operator/(ELEMENT_TYPE scalar) const \
    { \
        TYPE dest{}; \
        vector_detail::div_scalar(dest.arr, this->arr, scalar, static_cast<ELEMENT_TYPE>(ZERO_DIVISION_VALUE)); \
        return dest; \
    } \
    inline TYPE& operator+=(ELEMENT_TYPE scalar) \
    { \
        vector_detail::add_scalar(this->arr, this->arr, scalar); \
        return *this; \
    } \
    inline TYPE& operator-=(ELEMENT_TYPE scalar) \
    { \
        vector_detail::sub_scalar(this->arr, this->arr, scalar); \
        return *this; \
    } \
    inline TYPE& operator*=(ELEMENT_TYPE scalar) \
    { \
        vector_detail::mul_scalar(this->arr, this->arr, scalar); \
        return *this; \
    } \
    inline TYPE& operator/=(ELEMENT_TYPE scalar) \
    { \
        vector_detail::div_scalar(this->arr, this->arr, scalar, static_cast<ELEMENT_TYPE>(ZERO_DIVISION_VALUE)); \
        return *this; \
    }

#define CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(TYPE, ELEMENT_TYPE, ZERO_DIVISION_VALUE) \
    friend inline TYPE operator+(ELEMENT_TYPE scalar, const TYPE& value) \
    { \
        return value + scalar; \
    } \
    friend inline TYPE operator-(ELEMENT_TYPE scalar, const TYPE& value) \
    { \
        TYPE dest{}; \
        vector_detail::scalar_sub_from(dest.arr, scalar, value.arr); \
        return dest; \
    } \
    friend inline TYPE operator*(ELEMENT_TYPE scalar, const TYPE& value) \
    { \
        return value * scalar; \
    } \
    friend inline TYPE operator/(ELEMENT_TYPE scalar, const TYPE& value) \
    { \
        TYPE dest{}; \
        vector_detail::scalar_div_by_array(dest.arr, scalar, value.arr, static_cast<ELEMENT_TYPE>(ZERO_DIVISION_VALUE)); \
        return dest; \
    }

// INT

struct Vector2i {

    union
    {
        struct
        {
            int x;
            int y;
        };
        int arr[2]{};
    };

    constexpr Vector2i() = default;
    constexpr Vector2i(int vx, int vy) : x(vx), y(vy) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector2i, int, INT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector2i, int, INT_MAX)
};

struct Vector3i {

    union
    {
        struct
        {
            int x;
            int y;
            int z;
        };
        int arr[3]{};
    };

    constexpr Vector3i() = default;
    constexpr Vector3i(int vx, int vy, int vz) : x(vx), y(vy), z(vz) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector3i, int, INT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector3i, int, INT_MAX)
};

struct Vector4i {

    union
    {
        struct
        {
            int a;
            int b;
            int c;
            int d;
        };
        int arr[4]{};
    };

    constexpr Vector4i() = default;
    constexpr Vector4i(int va, int vb, int vc, int vd) : a(va), b(vb), c(vc), d(vd) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector4i, int, INT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector4i, int, INT_MAX)
};

struct Vector8i {

    union
    {
        struct
        {
            int a;
            int b;
            int c;
            int d;
            int e;
            int f;
            int g;
            int h;
        };
        int arr[8]{};
    };

    constexpr Vector8i() = default;

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector8i, int, INT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector8i, int, INT_MAX)
};


// FLOAT

struct Vector2f {

    union
    {
        struct
        {
            float x;
            float y;
        };
        float arr[2]{};
    };

    constexpr Vector2f() = default;
    constexpr Vector2f(float vx, float vy) : x(vx), y(vy) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector2f, float, FLT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector2f, float, FLT_MAX)
};

struct Vector3f {

    union
    {
        struct
        {
            float x;
            float y;
            float z;
        };
        float arr[3]{};
    };

    constexpr Vector3f() = default;
    constexpr Vector3f(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector3f, float, FLT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector3f, float, FLT_MAX)
};

struct Vector4f {

    union
    {
        struct
        {
            float a;
            float b;
            float c;
            float d;
        };
        float arr[4]{};
    };

    constexpr Vector4f() = default;
    constexpr Vector4f(float va, float vb, float vc, float vd) : a(va), b(vb), c(vc), d(vd) {}

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector4f, float, FLT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector4f, float, FLT_MAX)
};

struct Vector8f {

    union
    {
        struct
        {
            float a;
            float b;
            float c;
            float d;
            float e;
            float f;
            float g;
            float h;
        };
        float arr[8]{};
    };

    constexpr Vector8f() = default;

    CPP_GAME_ENGINE_ARRAY_METHODS(Vector8f, float, FLT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Vector8f, float, FLT_MAX)
};

// MATRIX

struct Matrix4f
{
    union
    {
        struct
        {
            float m00;
            float m01;
            float m02;
            float m03;

            float m10;
            float m11;
            float m12;
            float m13;

            float m20;
            float m21;
            float m22;
            float m23;

            float m30;
            float m31;
            float m32;
            float m33;
        };
        struct
        {
            Vector4f v0;
            Vector4f v1;
            Vector4f v2;
            Vector4f v3;
        };
        float arr[16]{};
    };

    constexpr Matrix4f() = default;

    CPP_GAME_ENGINE_ARRAY_METHODS(Matrix4f, float, FLT_MAX)
    CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS(Matrix4f, float, FLT_MAX)

    Matrix4f mat_mult(const Matrix4f& other) const;
    Matrix4f translate(const Vector3i& v) const;
    Matrix4f rotate(float rad, const Vector3i& axis) const;
    Matrix4f scale(float scale) const;

    static Matrix4f identity();
    static Matrix4f identity(float scale);
};

#undef CPP_GAME_ENGINE_SCALAR_FRIEND_METHODS
#undef CPP_GAME_ENGINE_ARRAY_METHODS
#undef CPP_GAME_ENGINE_HAS_AVX2
#undef CPP_GAME_ENGINE_HAS_AVX
#undef CPP_GAME_ENGINE_HAS_SSE41
#undef CPP_GAME_ENGINE_HAS_SSE
#undef CPP_GAME_ENGINE_HAS_SSE2

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

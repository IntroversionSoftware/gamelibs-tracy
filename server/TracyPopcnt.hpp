#ifndef __TRACYPOPCNT_HPP__
#define __TRACYPOPCNT_HPP__

#include <limits.h>
#include <stdint.h>

#if defined _M_AMD64
#  include <intrin.h>
#  define TracyCountBits __popcnt64
#  define TracyLzcnt __lzcnt64
#elif defined __GNUC__ || defined __clang__
static inline uint64_t TracyCountBits( uint64_t i )
{
    return uint64_t( __builtin_popcountll( i ) );
}
static inline uint64_t TracyLzcnt( uint64_t i )
{
    return uint64_t( __builtin_clzll( i ) );
}
#else
static inline uint64_t TracyCountBits( uint64_t i )
{
    return std::popcount(i);
}
static inline uint64_t TracyLzcnt( uint64_t i )
{
    return std::countl_zero(i);
}
#endif

#endif

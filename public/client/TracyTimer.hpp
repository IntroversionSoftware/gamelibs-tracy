#ifndef __TRACYTIMER_HPP__
#define __TRACYTIMER_HPP__

#include <chrono>
#include <cstdint>
#if defined _WIN32
#include <intrin.h>
#include <windows.h>
#endif
#if !defined _WIN32
#include <time.h>
#endif
#ifdef __APPLE__
#  include <TargetConditionals.h>
#  include <mach/mach_time.h>
#endif

#include "../common/TracyForceInline.hpp"

// TODO: Move these to some kind of common utility code location
#if __cplusplus >= 201803L
#define TRACY_LIKELY [[likely]]
#define TRACY_UNLIKELY [[unlikely]]
#else
#define TRACY_LIKELY
#define TRACY_UNLIKELY
#endif

#if defined _M_ARM || defined _M_ARM64 || defined __arm__ || defined __aarch64__
#define TRACY_LIKELY_ARM TRACY_LIKELY
#define TRACY_LIKELY_X86
#elif defined _M_IX86 || defined _M_AMD64 || defined __i386__ || defined __x86_64__
#define TRACY_LIKELY_ARM
#define TRACY_LIKELY_X86 TRACY_LIKELY
#else
#define TRACY_LIKELY_ARM
#define TRACY_LIKELY_X86
#endif

#undef TRACY_HAVE_CPU_CLOCK
#undef TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY

namespace tracy
{
namespace timers
{
namespace detail
{

static constexpr tracy_force_inline int64_t clock_scale_generic(int64_t _freq, int64_t _counter, int64_t _period_den)
{
    // Instead of just having "(_counter * _period_den) / _freq",
    // the algorithm below prevents overflow when _counter is sufficiently large.
    // It assumes that _freq * _period_den does not overflow, which is currently true for nano period.
    // It is not realistic for _counter to accumulate to large values from zero with this assumption,
    // but the initial value of _counter could be large.

    const int64_t whole = (_counter / _freq) * _period_den;
    const int64_t part = (_counter % _freq) * _period_den / _freq;
    return whole + part;
}

// Specialization: if the frequency value is 10MHz, use this function. The
// compiler recognizes the constants for frequency and time period and uses
// shifts and multiplies instead of divides to calculate the nanosecond value.
// This frequency is common on 64-bit x86 Windows.
static constexpr tracy_no_inline int64_t clock_scale_10MHz(int64_t _counter, int64_t _period_den)
{
    constexpr int64_t _freq = 10000000LL;
    return detail::clock_scale_generic(_freq, _counter, _period_den);
}

// Specialization: if the frequency value is 24MHz, use this function. The
// compiler recognizes the constants for frequency and time period and uses
// shifts and multiplies instead of divides to calculate the nanosecond value.
// This frequency is common on ARM64 (Windows devices, and Apple Silicon Macs)
static constexpr tracy_no_inline int64_t clock_scale_24MHz(int64_t _counter, int64_t _period_den)
{
    constexpr int64_t _freq = 24000000LL;
    return detail::clock_scale_generic(_freq, _counter, _period_den);
}

#ifdef _WIN32
static tracy_force_inline int64_t query_performance_frequency()
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart;
}

static tracy_force_inline int64_t query_performance_counter()
{
    LARGE_INTEGER ctr;
    QueryPerformanceCounter(&ctr);
    return ctr.QuadPart;
}
#endif

#if !defined(TRACY_HAVE_CPU_CLOCK)
#if defined(_M_ARM64)
#define TRACY_HAVE_CPU_CLOCK
#define TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY
#ifndef ARM64_CNTVCT
#define ARM64_CNTVCT ARM64_SYSREG(3, 3, 14, 0, 2)
#endif
#ifndef ARM64_CNTFRQ
#define ARM64_CNTFRQ ARM64_SYSREG(3, 3, 14, 0, 0)
#endif
#pragma intrinsic(_ReadStatusReg)
tracy_force_inline unsigned long long read_cpu_clock()
{
    return _ReadStatusReg(ARM64_CNTVCT);
}
tracy_force_inline unsigned long long read_cpu_clock_freq()
{
    return _ReadStatusReg(ARM64_CNTFRQ);
}
#endif
#endif

#if !defined(TRACY_HAVE_CPU_CLOCK)
#if defined(__aarch64__) || (defined(_M_ARM64) && defined(__clang__))
#define TRACY_HAVE_CPU_CLOCK
#define TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY
tracy_force_inline unsigned long long read_cpu_clock()
{
    unsigned long long cval;
    asm volatile("mrs %0, cntvct_el0"
                 : "=r"(cval));
    return cval;
}
tracy_force_inline unsigned long long read_cpu_clock_freq()
{
    unsigned long long cval;
    asm volatile("mrs %0, cntfrq_el0"
                 : "=r"(cval));
    return cval;
}
#endif
#endif
} // namespace detail

//
// std::chrono compatible clock sources
//
// Various high-resolution clocksources, with more highly optimized code paths
// than the C++ STL.
//

#if defined(CLOCK_MONOTONIC_RAW)
struct monotonic_raw
{
    using duration    = std::chrono::nanoseconds;
    using rep         = duration::rep;
    using period      = duration::period;
    using time_point  = std::chrono::time_point<monotonic_raw>;
    static const bool is_steady = true;

    static time_point now() noexcept
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        std::chrono::duration<rep, period> time(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
        return std::chrono::time_point<monotonic_raw>(time);
    }
};
#endif

#if defined(CLOCK_MONOTONIC)
struct monotonic
{
    using duration    = std::chrono::nanoseconds;
    using rep         = duration::rep;
    using period      = duration::period;
    using time_point  = std::chrono::time_point<monotonic>;
    static const bool is_steady = true;

    static time_point now() noexcept
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        std::chrono::duration<rep, period> time(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
        return std::chrono::time_point<monotonic>(time);
    }
};
#endif

#if defined(_WIN32)
struct win32_performance_counter
{
public:
    using duration    = std::chrono::nanoseconds;
    using rep         = duration::rep;
    using period      = duration::period;
    using time_point  = std::chrono::time_point<win32_performance_counter>;
    static const bool is_steady = true;

private:
    static tracy_no_inline int64_t qpc_scale_dispatch(int64_t _freq, int64_t _counter)
    {
        switch (_freq) {
        TRACY_LIKELY_X86 case 10000000LL: return detail::clock_scale_10MHz(_counter, period::den);
        TRACY_LIKELY_ARM case 24000000LL: return detail::clock_scale_24MHz(_counter, period::den);
        }
        return detail::clock_scale_generic(_freq, _counter, period::den);
    }

public:
    static time_point now() noexcept
    {
        static_assert(period::num == 1, "This assumes period::num == 1");
        const int64_t freq = detail::query_performance_frequency();
        const int64_t counter = detail::query_performance_counter();
        return time_point(duration(qpc_scale_dispatch(freq, counter)));
    }
};
#endif

#if defined(TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY)
struct cpu_clock
{
public:
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<cpu_clock>;
    static const bool is_steady = true;

private:
    static tracy_no_inline int64_t cpu_clock_scale_dispatch(int64_t _freq, int64_t _counter)
    {
        switch (_freq) {
        TRACY_LIKELY_ARM case 24000000LL: return detail::clock_scale_24MHz(_counter, period::den);
        }
        return detail::clock_scale_generic(_freq, _counter, period::den);
    }

public:
    static time_point now() noexcept
    {
        static_assert(period::num == 1, "This assumes period::num == 1");
        const int64_t freq = detail::read_cpu_clock_freq();
        const int64_t counter = detail::read_cpu_clock();
        return time_point(duration(cpu_clock_scale_dispatch(freq, counter)));
    }
};
#endif

} // namespace timers

#if defined(TRACY_TIMER_FALLBACK)

using high_res_time = std::chrono::steady_clock;

#else

#if defined(TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY)
using high_res_time = timers::cpu_clock;
#elif defined(CLOCK_MONOTONIC_RAW)
using high_res_time = timers::monotonic_raw;
#elif defined(CLOCK_MONOTONIC)
using high_res_time = timers::monotonic;
#elif defined(_WIN32)
using high_res_time = timers::win32_performance_counter;
#else
using high_res_time = std::chrono::steady_clock;
#endif

#endif

}

#undef TRACY_LIKELY_X86
#undef TRACY_LIKELY_ARM
#undef TRACY_HAVE_CPU_CLOCK
#undef TRACY_HAVE_CPU_CLOCK_KNOWN_FREQUENCY

#endif

#include "performance_clock.hpp"

#include <sys/resource.h>

using std::chrono::time_point;
using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;

namespace performance_clock
{
    void interval::before()
    {
        // Record user time, system time, page faults
        rusage r_usage;
        getrusage(RUSAGE_SELF, &r_usage);
        usr_time_before_ =
            r_usage.ru_utime.tv_sec * static_cast<uint64_t>(1000000000) +
            r_usage.ru_utime.tv_usec * static_cast<uint64_t>(1000);
        sys_time_before_ =
            r_usage.ru_stime.tv_sec * static_cast<uint64_t>(1000000000) +
            r_usage.ru_stime.tv_usec * static_cast<uint64_t>(1000);
        // min_faults_before_ = r_usage.ru_minflt;
        // maj_faults_before_ = r_usage.ru_majflt;

        // Record wall time from std::chrono
        wall_time_before_ = high_resolution_clock::now();
    }

    void interval::after()
    {
        // Record wall time from std::chrono
        wall_time_ = duration_cast<nanoseconds>(
            high_resolution_clock::now() - wall_time_before_).count();

        // Record user time, system time, page faults
        rusage r_usage;
        getrusage(RUSAGE_SELF, &r_usage);
        usr_time_ =
            r_usage.ru_utime.tv_sec * static_cast<uint64_t>(1000000000) +
            r_usage.ru_utime.tv_usec * static_cast<uint64_t>(1000)
            - usr_time_before_;
        sys_time_ =
            r_usage.ru_stime.tv_sec * static_cast<uint64_t>(1000000000) +
            r_usage.ru_stime.tv_usec * static_cast<uint64_t>(1000)
            - sys_time_before_;
        // min_faults_ = r_usage.ru_minflt - min_faults_before_;
        // maj_faults_ = r_usage.ru_majflt - maj_faults_before_;
    }
}

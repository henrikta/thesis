#pragma once

#include <chrono>
#include <cstdint>

namespace performance_clock
{
    class interval
    {
    public:
        void before();
        void after();

        uint64_t wall_time() const { return wall_time_; }
        uint64_t usr_time() const { return usr_time_; }
        uint64_t sys_time() const { return sys_time_; }
        // uint64_t min_faults() const { return min_faults_; }
        // uint64_t maj_faults() const { return maj_faults_; }

    private:
        using time_point =
            std::chrono::time_point<std::chrono::high_resolution_clock>;

        time_point wall_time_before_;
        uint64_t usr_time_before_;
        uint64_t sys_time_before_;
        // uint64_t min_faults_before_;
        // uint64_t maj_faults_before_;

        uint64_t wall_time_;
        uint64_t usr_time_;
        uint64_t sys_time_;
        // uint64_t min_faults_;
        // uint64_t maj_faults_;
    };
}


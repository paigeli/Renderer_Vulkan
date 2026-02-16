#pragma once

#include <chrono>
#include <functional>

struct Timer {
    using clock = std::chrono::high_resolution_clock;
    
    Timer(std::function<void (double)> const &cb_) :
        cb(cb_),
        before(clock::now())
        {}

    ~Timer() {
        auto after = clock::now();
        std::chrono::duration<double> elapsed = after - before;
        cb(elapsed.count());
    }

    std::function<void (double)> cb;
    clock::time_point before;
};
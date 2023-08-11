#pragma once

#include <atomic>

using namespace std;


class SpinLock {
private:
    atomic_flag spin_lock_flag;

public:
    SpinLock(): spin_lock_flag(false) {}

    inline void lock() {
        while (this->spin_lock_flag.test_and_set(memory_order_acquire)) {
            //spin
        }
    }

    inline void unlock() {
        this->spin_lock_flag.clear(memory_order_release);
    }

};
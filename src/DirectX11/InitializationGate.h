#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

// Publish configuration and forwarding entry points together. A callback on
// the owning thread must fail rather than deadlock or observe partial state.
class InitializationGate {
public:
    template<class Initializer>
    bool Run(Initializer&& initialize)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (state_ == State::INITIALIZING) {
            if (owner_ == std::this_thread::get_id()) return false;
            changed_.wait(lock);
        }
        if (state_ == State::READY) return true;
        state_ = State::INITIALIZING;
        owner_ = std::this_thread::get_id();
        lock.unlock();
        bool ready;
        try {
            ready = std::forward<Initializer>(initialize)();
        } catch (...) {
            Finish(false);
            throw;
        }
        Finish(ready);
        return ready;
    }

private:
    enum class State { IDLE, INITIALIZING, READY };
    std::mutex mutex_;
    std::condition_variable changed_;
    State state_ = State::IDLE;
    std::thread::id owner_;

    void Finish(bool ready)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = ready ? State::READY : State::IDLE;
            owner_ = {};
        }
        changed_.notify_all();
    }
};

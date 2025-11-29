//
// Created by jwd on 2025/11/27.
//

#ifndef RTSP_PUBLISH_THREADSAFEQUEUE_H
#define RTSP_PUBLISH_THREADSAFEQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cond_.notify_one();
    }

    void push_latest(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            T old_val = queue_.front();
            queue_.pop();
            av_frame_unref(old_val);
            av_frame_free(&old_val);
        }
        queue_.push(std::move(value));
        cond_.notify_one();
    }

    bool pop(T &value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || !running_; });
        if (!running_ && queue_.empty()) {
            return false;
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

    bool pop_timeout(T &value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty() || !running_; });
        if (!running_ && queue_.empty()) {
            return false;
        }
        if (queue_.empty()) {
            return false; // Timeout
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cond_.notify_one();
    }


private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> running_{true};
};


#endif //RTSP_PUBLISH_THREADSAFEQUEUE_H
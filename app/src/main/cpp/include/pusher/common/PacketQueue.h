#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace pusher {

/**
 * 有界阻塞队列，用于编码器 -> 推流线程的数据传递。
 * 队列满时可选择丢弃最旧数据，避免内存无限增长。
 */
template <typename T>
class PacketQueue {
public:
    explicit PacketQueue(size_t maxSize) : maxSize_(maxSize) {}

    void push(T item, bool dropOldestWhenFull = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= maxSize_) {
            if (!dropOldestWhenFull) {
                queue_.pop_front();
            } else {
                return;
            }
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
    }

    bool pop(T& item, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !queue_.empty() || finished_; })) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool waitForItem(int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                            [this] { return !queue_.empty() || finished_; });
    }

    bool peek(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = queue_.front();
        return true;
    }

    bool popNoWait(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cv_.notify_all();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    size_t maxSize_;
    bool finished_ = false;
};

}  // namespace pusher

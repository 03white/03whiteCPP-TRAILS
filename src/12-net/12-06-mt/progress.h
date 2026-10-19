#pragma once
#include <mutex>
#include <functional>
using ProgressCallback = std::function<void(size_t total, size_t now)>;
struct Progress {
    void add(size_t delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        downloaded_size += delta;
        if (callback) {
            callback(total_size, downloaded_size);
        }
    }
    size_t get() {
        std::lock_guard<std::mutex> lock(mutex_);
        return downloaded_size;
    }
    void setCallback(ProgressCallback callback) {
        this->callback = std::move(callback);
    }
    std::mutex mutex_;
    size_t total_size{0};
    size_t downloaded_size{0};
    ProgressCallback callback;
};
#pragma once
#include "metric.h"

#include <deque>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <chrono>

// Буфер для метрик.
class MetricBuffer {
public:
    explicit MetricBuffer(size_t max_size = 100) : max_size(max_size) {}

    // Добавление записи в буфер.
    bool push(const ActivityMetric& metric) {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.size() >= max_size) {
            dropped++;
            return false; // Если места нет false.
        }
        queue.push_back(metric);
        cv.notify_one(); 
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }

    size_t droppedCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return dropped;
    }

    // Берем первые N элементов для отправки. Не удаляя их отсюда
    std::vector<ActivityMetric> snapshot(size_t max_count = 100) const {
        std::lock_guard<std::mutex> lock(mtx);
        size_t count = std::min(max_count, queue.size());
        std::vector<ActivityMetric> res;
        res.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            res.push_back(queue[i]);
        }
        return res;
    }

    // Удаляем успешно отправленные записи
    bool erasePrefix(size_t count) {
        std::lock_guard<std::mutex> lock(mtx);
        if (count > queue.size()) return false;
        for (size_t i = 0; i < count; ++i) {
            queue.pop_front();
        }
        return true;
    }

    // Ожидание, пока буфер не опустеет или не истечет время
    template <typename Predicate>
    void waitFor(std::chrono::milliseconds timeout, Predicate pred) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, timeout, pred);
    }

    void notifyAll() {
        cv.notify_all();
    }

private:
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::deque<ActivityMetric> queue;
    size_t max_size;
    size_t dropped = 0;
};
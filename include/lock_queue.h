#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

// 异步日志队列
template<typename T>
class LockQueue
{
public:
    void push(const T& data)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(data);
        m_cv.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]{
            return !m_queue.empty();
        });
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }

    void drain(std::queue<T>& out)
    {
        // 清空传入队列
        if(!out.empty()) {
            out = {};
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.swap(out);
    }
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};
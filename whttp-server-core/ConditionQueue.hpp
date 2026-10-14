#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>
#include <utility>

// 可被多个队列共享的同步对象（一把互斥量 + 一个条件变量）。
// 多个队列共用一个同步对象时，消费线程能在同一个锁/谓词里同时等待多个队列，
// 例如"视频队列或音频队列任一非空"。
class QueueSync
{
public:
    std::mutex &mutex()
    {
        return _mutex;
    }

    void wait(std::unique_lock<std::mutex> &locker)
    {
        _condition.wait(locker);
    }

    // 谓词版：SFINAE约束谓词必须可调用，否则传整数实参时会被本模板抢走
    template <typename Pred>
    auto wait(std::unique_lock<std::mutex> &locker, Pred pred) -> decltype(pred(), void())
    {
        _condition.wait(locker, pred);
    }

    // 超时版（无谓词），返回std::cv_status，语义与cv.wait_for(locker, timeout)一致；
    // timeoutMs < 0 表示一直等待，此时恒返回no_timeout
    std::cv_status waitFor(std::unique_lock<std::mutex> &locker, int64_t timeoutMs)
    {
        if (timeoutMs < 0)
        {
            _condition.wait(locker);
            return std::cv_status::no_timeout;
        }
        return _condition.wait_for(locker, std::chrono::milliseconds(timeoutMs));
    }

    // 带超时的谓词版，返回唤醒后谓词的结果：true=条件成立，false=超时且条件不成立；
    // timeoutMs < 0 表示一直等待（等价于wait(locker, pred)）
    template <typename Pred>
    auto waitFor(std::unique_lock<std::mutex> &locker, int64_t timeoutMs, Pred pred) -> decltype(pred(), bool())
    {
        if (timeoutMs < 0)
        {
            _condition.wait(locker, pred); // 不带超时的wait返回void，谓词结果需自行取
            return pred();
        }
        return _condition.wait_for(locker, std::chrono::milliseconds(timeoutMs), pred);
    }

    void notifyOne()
    {
        _condition.notify_one();
    }

    void notifyAll()
    {
        _condition.notify_all();
    }

private:
    std::mutex _mutex;
    std::condition_variable _condition;
};

// 通用阻塞队列，用法与std::condition_variable一致：调用方自行持锁、自行写谓词，
// 队列数据与等待条件共用同一把锁，"判空-wait"在同一锁内完成，
// 从根上避免"锁外判空 + 另一把锁wait"双锁模式的丢失唤醒问题。
// 纯C++11标准库实现，不依赖Qt。
//
// 使用约束：
// - 消费端：用内层作用域包住 lock + wait(谓词)，出作用域自动放锁，之后再用deQueue取出
// - 谓词里带上自己的退出标志（如!_workFlag），停止方先置标志再调notifyAll()，否则线程卡在wait里出不来
// - enQueue/deQueue内部会加锁，不可在持锁状态下调用；size()/empty()为无锁读，谓词里可安全调用
// - 需要同时等待多个队列时，用同一个QueueSync构造这些队列；
//   该QueueSync必须在队列之前声明（先声明先析构，保证生命周期长于队列）
// - enQueue内部用notifyOne唤醒（每个同步对象只有一个消费线程），停止路径用notifyAll
template <typename T>
class ConditionQueue
{
public:
    ConditionQueue() : _sync(&_ownSync)
    {
        QueueNode *node = new QueueNode();
        node->next = nullptr;
        // head->next is the first node, _tail point to last node, not _tail->next
        _head = node;
        _tail = _head;
    }

    explicit ConditionQueue(QueueSync &sync) : _sync(&sync)
    {
        QueueNode *node = new QueueNode();
        node->next = nullptr;
        _head = node;
        _tail = _head;
    }

    ~ConditionQueue()
    {
        clear();
        delete _head;
        _head = nullptr;
        _tail = nullptr;
    }

    ConditionQueue(const ConditionQueue &) = delete;
    ConditionQueue &operator=(const ConditionQueue &) = delete;

    struct QueueNode
    {
        T value;
        QueueNode *next;
    };

    QueueSync &sync()
    {
        return *_sync; // 传给其它队列，使多个队列共用一把锁和一个条件变量
    }

    std::mutex &mutex()
    {
        return _sync->mutex();
    }

    void wait(std::unique_lock<std::mutex> &locker)
    {
        _sync->wait(locker);
    }

    template <typename Pred>
    auto wait(std::unique_lock<std::mutex> &locker, Pred pred) -> decltype(pred(), void())
    {
        _sync->wait(locker, pred);
    }

    std::cv_status waitFor(std::unique_lock<std::mutex> &locker, int64_t timeoutMs)
    {
        return _sync->waitFor(locker, timeoutMs);
    }

    template <typename Pred>
    auto waitFor(std::unique_lock<std::mutex> &locker, int64_t timeoutMs, Pred pred) -> decltype(pred(), bool())
    {
        return _sync->waitFor(locker, timeoutMs, pred);
    }

    void notifyOne()
    {
        _sync->notifyOne();
    }

    void notifyAll()
    {
        _sync->notifyAll();
    }

    bool enQueue(T data)
    {
        // 聚合初始化一步构造：value构造若抛异常，new-expression保证自动释放内存，不泄漏
        QueueNode *node = new (std::nothrow) QueueNode{std::move(data), nullptr};
        if (!node)
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> locker(_sync->mutex());
            _tail->next = node;
            _tail = node;
            _queueSize++;
        }

        return true;
    }

    bool deQueue(T &data)
    {
        std::lock_guard<std::mutex> locker(_sync->mutex());
        QueueNode *currentFirstNode = _head->next;
        if (!currentFirstNode)
        {
            return false;
        }

        _head->next = currentFirstNode->next;
        data = std::move(currentFirstNode->value);
        delete currentFirstNode;
        _queueSize--;
        if (_queueSize == 0)
        {
            _tail = _head;
        }
        return true;
    }

    int64_t size()
    {
        return _queueSize.load();
    }

    bool empty()
    {
        return (_queueSize.load() <= 0);
    }

    void clear()
    {
        T data;
        while (deQueue(data));
    }

private:
    QueueSync _ownSync; // 独占场景用；共享场景下不使用，_sync指向外部传入的对象
    QueueSync *_sync;

    QueueNode *_head;
    QueueNode *_tail;
    std::atomic<int64_t> _queueSize{0}; // 增减在锁内进行，读取(size/empty)无锁
};
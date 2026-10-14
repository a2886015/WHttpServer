#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>
#include <utility>

template <typename T>
class LockQueue
{
public:
    LockQueue()
    {
        QueueNode *node = new QueueNode();
        node->next = nullptr;
        // head->next is the first node, _tail point to last node, not _tail->next
        _head = node;
        _tail = _head;
    };
    virtual ~LockQueue()
    {
        clear();
        delete _head;
        _head =  nullptr;
        _tail = nullptr;
    };

    struct QueueNode
    {
        T value;
        QueueNode *next;
    };

    bool enQueue(T data)
    {
        // 聚合初始化一步构造：value构造若抛异常，new-expression保证自动释放内存，不泄漏
        QueueNode *node = new (std::nothrow) QueueNode{std::move(data), nullptr};
        if (!node)
        {
            return false;
        }

        std::unique_lock<std::mutex> locker(_mutex);
        _tail->next = node;
        _tail = node;
        _queueSize++;
        return true;
    }

    bool deQueue(T &data)
    {
        std::unique_lock<std::mutex> locker(_mutex);
        QueueNode *currentFirstNode = _head->next;
        if (!currentFirstNode)
        {
            return false;
        }

        _head->next = currentFirstNode->next;
        data = std::move(currentFirstNode->value);
        delete  currentFirstNode;
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

    void clear()
    {
        T data;
        while(deQueue(data));
    }

    bool empty()
    {
        return (_queueSize.load() <= 0);
    }
private:
    QueueNode *_head;
    QueueNode *_tail;
    std::atomic<int64_t> _queueSize = {0}; // size()/empty()为无锁读，与锁内增减并发，必须原子
    std::mutex _mutex;
};


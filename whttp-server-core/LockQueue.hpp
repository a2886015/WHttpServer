#pragma once

#include <mutex>
#include <iostream>

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
        QueueNode *node = new (std::nothrow) QueueNode();
        if (!node)
        {
            return false;
        }
        node->value = data;
        node->next = nullptr;

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
        data = currentFirstNode->value;
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
        return _queueSize;
    }

    void clear()
    {
        T data;
        while(deQueue(data));
    }

    bool empty()
    {
        return (_queueSize <= 0);
    }
private:
    QueueNode *_head;
    QueueNode *_tail;
    int64_t _queueSize = 0;
    std::mutex _mutex;
};


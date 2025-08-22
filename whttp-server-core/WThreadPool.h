#pragma once

#include <functional>
#include <mutex>
#include <list>
#include <thread>
#include <memory>
#include <atomic>
#include <stdio.h>
#include <map>
#include <sstream>
#include <condition_variable>
#include <future>
#include "LockQueue.hpp"
#include <assert.h>

#define WThreadPool_log(fmt, ...) {printf(fmt, ##__VA_ARGS__);printf("\n");fflush(stdout);}

#define WPOOL_MIN_THREAD_NUM 4
#define WPOOL_MAX_THREAD_NUM 256
#define WPOOL_MANAGE_SECONDS 20
#define ADD_THREAD_BOUNDARY 1

using EventFun = std::function<void ()>;
using int64 = long long int;

class WThreadPool
{
public:
    WThreadPool();
    virtual ~WThreadPool();
    WThreadPool(const WThreadPool &) = delete;
    WThreadPool& operator=(const WThreadPool &) = delete;

    static WThreadPool * globalInstance();

    void setMaxThreadNum(int maxNum);
    bool waitForDone(int waitMs = -1);

    /*
    template<typename Func, typename ...Arguments >
    void concurrentRun(Func &&func, Arguments&&... args) {
        EventFun queunFun = std::bind(func, std::forward<Arguments>(args)...);
        enQueueEvent(queunFun);
        if (((int)_workThreadList.size() < _maxThreadNum) &&
                (_eventQueue.size() >= ((int)_workThreadList.size() - _busyThreadNum - ADD_THREAD_BOUNDARY)))
        {
           _mgrCondVar.notify_one();
        }
        _workCondVar.notify_one();
    }
    */

    template<typename Func, typename... Arguments>
    auto concurrentRun(Func &&func, Arguments&&... args)
        -> std::future<decltype(std::bind(std::forward<Func>(func), std::forward<Arguments>(args)...)())>
    {
        using return_type = decltype(std::bind(std::forward<Func>(func), std::forward<Arguments>(args)...)());

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<Func>(func), std::forward<Arguments>(args)...)
        );

        std::future<return_type> res = task->get_future();
        EventFun queueFun = [task]() { (*task)(); };

        enQueueEvent(queueFun);

        // 线程管理逻辑
        if (((int)_workThreadList.size() < _maxThreadNum) &&
            (_eventQueue.size() >= ((int)_workThreadList.size() - _busyThreadNum - ADD_THREAD_BOUNDARY))) {
            _mgrCondVar.notify_one();
        }
        _workCondVar.notify_one();

        return res;
    }

    template<typename T> static int64_t threadIdToint64(T threadId)
    {
        std::string stid;
        stid.resize(32);
        snprintf((char *)stid.c_str(), 32, "%lld", threadId);
        long long int tid = std::stoll(stid);
        return tid;
    }

private:
    int _minThreadNum = 4;
    int _maxThreadNum = 8;
    std::atomic<int> _busyThreadNum = {0};
    int _stepThreadNum = 4;
    volatile bool _exitAllFlag = false;
    std::atomic<int> _reduceThreadNum = {0};

    std::shared_ptr<std::thread> _mgrThread;
    LockQueue<EventFun> _eventQueue;
    std::list<std::shared_ptr<std::thread>> _workThreadList;

    std::mutex _threadIsRunMutex;
    std::map<std::thread::id, bool> _threadIsRunMap;

    std::condition_variable _workCondVar;
    std::mutex _workMutex;
    std::condition_variable _mgrCondVar;
    std::mutex _mgrMutex;

    static std::shared_ptr<WThreadPool> s_threadPool;
    static std::mutex s_globleMutex;

    void enQueueEvent(EventFun fun);
    EventFun  deQueueEvent();
    void run();
    void managerThread();
    void stop();
    void startWorkThread();
    void stopWorkThread();
    void adjustWorkThread();
};


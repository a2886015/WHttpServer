#include "WThreadPool.h"

using namespace std;

shared_ptr<WThreadPool> WThreadPool::s_threadPool;
std::mutex WThreadPool::s_globleMutex;

WThreadPool::WThreadPool()
{
    _minThreadNum = WPOOL_MIN_THREAD_NUM;
    _mgrThread = make_shared<thread>(&WThreadPool::managerThread, this);
}

WThreadPool::~WThreadPool()
{
    stop();
}

WThreadPool *WThreadPool::globalInstance()
{
    if (!s_threadPool.get())
    {
        unique_lock<mutex> locker(s_globleMutex);
        if (!s_threadPool.get())
        {
            s_threadPool = make_shared<WThreadPool>();
        }
    }
    return s_threadPool.get();
}

void WThreadPool::setMaxThreadNum(int maxNum)
{
    if (maxNum > WPOOL_MAX_THREAD_NUM)
    {
        maxNum = WPOOL_MAX_THREAD_NUM;
    }
    else if (maxNum < WPOOL_MIN_THREAD_NUM)
    {
        maxNum = WPOOL_MIN_THREAD_NUM;
    }
    _maxThreadNum = maxNum;
}

bool WThreadPool::waitForDone(int waitMs)
{
    int waitedMs = 0;
    auto startTime = std::chrono::steady_clock::now();
    while(_busyThreadNum != 0 || !_eventQueue.empty())
    {
        this_thread::sleep_for(chrono::milliseconds(5));
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
        waitedMs = duration.count();
        if (waitMs > 0 && waitedMs >= waitMs)
        {
            return false;
        }
    }
    return true;
}

void WThreadPool::enQueueEvent(EventFun fun)
{
    bool res = _eventQueue.enQueue(fun);
    assert(res);
}

EventFun WThreadPool::deQueueEvent()
{
    EventFun fun;
    if (_eventQueue.deQueue(fun))
    {
        return fun;
    }
    else
    {
        return nullptr;
    }
    return fun;
}

void WThreadPool::run()
{
    {
        unique_lock<mutex> locker(_threadIsRunMutex);
        _threadIsRunMap[this_thread::get_id()] = true;
    }

    while (!_exitAllFlag)
    {
        {
            unique_lock<mutex> locker(_workMutex);
            if (_eventQueue.empty() && !_exitAllFlag)
            {
                _workCondVar.wait(locker);
            }

            if (_reduceThreadNum > 0)
            {
                _reduceThreadNum--;
                break;
            }
        }

        _busyThreadNum++;
        while (!_exitAllFlag)
        {
            EventFun fun = deQueueEvent();
            if (!fun)
            {
                break;
            }
            fun();
        }
        _busyThreadNum--;
    }

    {
        unique_lock<mutex> locker(_threadIsRunMutex);
        _threadIsRunMap[this_thread::get_id()] = false;
    }
}

void WThreadPool::stop()
{
    _exitAllFlag = true;
    {
        unique_lock<mutex> locker(_mgrMutex);
        _mgrCondVar.notify_all();
    }

    if (_mgrThread->joinable())
    {
        _mgrThread->join();
    }
}

void WThreadPool::managerThread()
{
    startWorkThread();
    while (!_exitAllFlag)
    {
        {
            unique_lock<mutex> locker(_mgrMutex);
            auto now = std::chrono::steady_clock::now();
            if (((int)_workThreadList.size() >= _maxThreadNum ||
                 _eventQueue.size() < ((int)_workThreadList.size() - _busyThreadNum - ADD_THREAD_BOUNDARY)) && !_exitAllFlag)
            {
                _mgrCondVar.wait_until(locker, now + chrono::seconds(WPOOL_MANAGE_SECONDS));
            }
        }

        if (_exitAllFlag)
        {
            break;
        }

        adjustWorkThread();
        // WThreadPool_log("show work thread num:%d", _workThreadList.size());
    }
    stopWorkThread();
}

void WThreadPool::startWorkThread()
{
    for (int i = 0; i < _minThreadNum; i++)
    {
        shared_ptr<thread> threadPtr = make_shared<thread>(&WThreadPool::run, this);
        _workThreadList.emplace_back(threadPtr);
    }
}

void WThreadPool::stopWorkThread()
{
    {
        unique_lock<mutex> locker(_workMutex);
        _workCondVar.notify_all();
    }

    for (auto it = _workThreadList.begin(); it != _workThreadList.end(); it++)
    {
        if ((*it)->joinable())
        {
            (*it)->join();
        }
    }
    _workThreadList.clear();
    _threadIsRunMap.clear();
    _eventQueue.clear();
}

void WThreadPool::adjustWorkThread()
{
    int queueSize = _eventQueue.size();
    int busyThreadNum = _busyThreadNum;
    int liveThreadNum = _workThreadList.size();
    int maxThreadNum = _maxThreadNum;
    int stepThreadNum = _stepThreadNum;
    int minThreadNum = _minThreadNum;

    // if rest thread can not run all task concurrently, add the thread
    if ((liveThreadNum < maxThreadNum) && (queueSize >= (liveThreadNum - busyThreadNum - ADD_THREAD_BOUNDARY)))
    {
        int restAllAddNum = maxThreadNum - liveThreadNum;
        int addThreadNum = restAllAddNum > stepThreadNum ? stepThreadNum : restAllAddNum;
        for (int i = 0; i < addThreadNum; i++)
        {
            shared_ptr<thread> threadPtr = make_shared<thread>(&WThreadPool::run, this);
            _workThreadList.emplace_back(threadPtr);
        }
    }
    else if ((liveThreadNum > minThreadNum) && (busyThreadNum*2 < liveThreadNum))
    {
        int resAllReduceNum = liveThreadNum - minThreadNum;
        int reduceThreadNum = resAllReduceNum > stepThreadNum ? stepThreadNum : resAllReduceNum;
        _reduceThreadNum = reduceThreadNum;
        int findExitThreadNum = 0;
        do
        {
            if (_exitAllFlag)
            {
                return;
            }

            for (int i = 0; i < (reduceThreadNum - findExitThreadNum); i++)
            {
                _workCondVar.notify_one();
            }

            this_thread::sleep_for(chrono::milliseconds(1));

            {
                unique_lock<mutex> locker(_threadIsRunMutex);
                for (auto it = _workThreadList.begin(); it != _workThreadList.end();)
                {
                    std::thread::id threadId = (*it)->get_id();
                    auto threadIdIt = _threadIsRunMap.find(threadId);
                    if ((threadIdIt != _threadIsRunMap.end()) && (_threadIsRunMap[threadId] == false))
                    {
                        findExitThreadNum++;
                        _threadIsRunMap.erase(threadIdIt);
                        (*it)->join();
                        _workThreadList.erase(it++);
                    }
                    else
                    {
                        it++;
                    }
                }
            }

            /*
            WThreadPool_log("show findExitThreadNum:%d, reduceThreadNum:%d, _reduceThreadNum:%d", findExitThreadNum, reduceThreadNum, (int)_reduceThreadNum);
            for (auto it = _workThreadList.begin(); it != _workThreadList.end(); it++)
            {
                WThreadPool_log("work thread pid:%lld", (*it)->get_id());
            }
            for (auto it = _threadIsRunMap.begin(); it != _threadIsRunMap.end(); it++)
            {
                WThreadPool_log("it->first:%lld, it->second:%d", it->first, it->second);
            }
            */

        } while(!(findExitThreadNum >= reduceThreadNum && _reduceThreadNum <= 0));
    }
}

#pragma once

#include <string>
#include <map>
#include <memory>
#include "mongoose.h"
#include "LockQueue.hpp"
#include <time.h>
#include <functional>
#include <sstream>
#include "WThreadPool.h"

using namespace std;

#define WLogi(fmt, ...) {time_t timep; time(&timep); struct tm *p = localtime(&timep); printf("%4d-%02d-%02d %02d:%02d:%02d [%llu][I] ", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p-> tm_hour, p->tm_min, p->tm_sec, (unsigned long long)pthread_self());printf(fmt, ##__VA_ARGS__);printf("\n");fflush(stdout);}
#define WLogw(fmt, ...) {time_t timep; time(&timep); struct tm *p = localtime(&timep); printf("%4d-%02d-%02d %02d:%02d:%02d [%llu][W] ", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p-> tm_hour, p->tm_min, p->tm_sec, (unsigned long long)pthread_self());printf(fmt, ##__VA_ARGS__);printf("\n");fflush(stdout);}
#define WLoge(fmt, ...) {time_t timep; time(&timep); struct tm *p = localtime(&timep); printf("%4d-%02d-%02d %02d:%02d:%02d [%llu][E] ", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p-> tm_hour, p->tm_min, p->tm_sec, (unsigned long long)pthread_self());printf(fmt, ##__VA_ARGS__);printf("\n");fflush(stdout);}

#define W_HTTP_GET      (1 << 0)
#define W_HTTP_POST     (1 << 1)
#define W_HTTP_PUT      (1 << 2)
#define W_HTTP_DELETE   (1 << 3)
#define W_HTTP_HEAD     (1 << 4)
#define W_HTTP_OPTIONS  (1 << 5)
#define W_HTTP_ALL      0xFF

using HttpChunkQueue = LockQueue<shared_ptr<string>>;
using HttpSendQueue = LockQueue<shared_ptr<string>>;

#ifdef WIN32
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
#endif

struct WCaseCompare
{
    bool operator() (const std::string& lhs, const std::string& rhs) const
    {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};

struct HttpReqMsg
{
   mg_connection *httpConnection = nullptr;
   string method; // GET POST PUT DELETE
   string uri;
   map<string, string> querys; // the params in uri
   string proto; // http version
   // the params in header, all key letters is no case-sensitive, eg "Content-Length" and "content-length" do same
   map<string, string, WCaseCompare> headers;
   string body;
   int64_t totalBodySize;
   shared_ptr<HttpChunkQueue> chunkQueue;
   shared_ptr<HttpSendQueue> sendQueue;
   int64_t recvChunkSize = 0;
   bool finishRecvChunk = false;
   bool isKeepingAlive = false;
   int64_t lastKeepAliveTime = 0;
};

using HttpCbFun = std::function<void(shared_ptr<HttpReqMsg> &)>;
using HttpFilterFun = std::function<bool(shared_ptr<HttpReqMsg> &)>;
using TimerEventFun = std::function<void()>;

class IHttpServer
{
public:
    IHttpServer(mg_mgr * = nullptr){}
    virtual ~IHttpServer(){}
    virtual bool init(int maxEventThreadNum, WThreadPool *threadPool = nullptr) = 0;
    virtual bool startHttp(int port) = 0;
    virtual bool startHttps(int port, string certPath, string keyPath) = 0;
    virtual bool stop() = 0;
    virtual bool run(int timeoutMs) = 0;
    virtual bool isRunning() = 0;
    virtual void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods) = 0;
    virtual void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods) = 0;
    virtual void setHttpFilter(HttpFilterFun filter) = 0;
    virtual void forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg) = 0;
    virtual void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body) = 0;
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len) = 0; // data需要外部delete
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, shared_ptr<string> &sendMsg) = 0;
    virtual string formJsonBody(int code, string message) = 0;
    virtual bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg) = 0;
    virtual shared_ptr<string> deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg) = 0;
    virtual bool addStaticWebDir(const string &dir, const string &header = "") = 0;
    virtual uint16_t addTimerEvent(unsigned long ms, TimerEventFun timerEventFun) = 0;
    virtual bool deleteTimerEvent(uint16_t timerEventId) = 0;
    virtual bool deleteAllTimerEvent() = 0;
};

#pragma once

#include "IHttpServer.h"
#include <set>
#include <vector>
#include <time.h>
#include <atomic>
#include <queue>

#define HTTP_SEND_QUEUE_SIZE 3
#define SEND_BUF_SIZE_BOUNDARY (3 * 1024 * 1024)
#define CHUNK_QUEUE_SIZE_BOUNDARY 2000
#define HTTP_MAX_HEAD_SIZE (2 * 1024 * 1024)

#define HTTP_UNKNOWN_REQUEST 100
#define HTTP_BEYOND_HEAD_SIZE 101

#define MAX_KEEP_ALIVE_NUM 100
#define KEEP_ALIVE_TIME 5 // 5s
#define MAX_DOWNLOAD_PAUSE_TIME 60 // 60s

class WHttpServer;
struct WHttpServerCbMsg
{
   WHttpServer *httpServer = nullptr;
   bool httpsFlag = false;
};

struct WHttpServerApiData
{
    HttpCbFun httpCbFun = nullptr;
    int httpMethods = -1;
    bool findStaticFileFlag = false;
};

struct WHttpStaticWebDir
{
    string dirPath = "";
    string header = "";
};

struct WTimerData
{
    mg_timer timer;
    WTimerEventFun timerFun;
    WTimerRunType runType;
    WHttpServer *httpServer = nullptr;
    int64_t timeId = 0;
};

using WHttpLoopFun = std::function<void ()>;

class WHttpServer: public IHttpServer
{
public:
    WHttpServer(mg_mgr *mgr = nullptr);
    virtual ~WHttpServer();
    virtual bool init(int maxEventThreadNum, WThreadPool *threadPool = nullptr);
    virtual bool startHttp(int port);
    virtual bool startHttps(int port, string certPath, string keyPath);
    virtual bool stop(); // mg_mgr是外部传入时，外部需要主动调用stop函数
    virtual bool run(int timeoutMs);
    virtual bool isRunning();
    virtual void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void setHttpFilter(HttpFilterFun filter);
    virtual void forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg);
    virtual void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, shared_ptr<string> &sendMsg);
    virtual string formJsonBody(int code, string message);
    virtual bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg);
    virtual shared_ptr<string> deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg);
    virtual bool addStaticWebDir(const string &dir, const string &header = "");
    virtual uint64_t addTimerEvent(unsigned long ms, WTimerEventFun timerEventFun, WTimerRunType runType = WTimerRunRepeat);
    virtual bool deleteTimerEvent(uint64_t timerEventId);
    virtual bool deleteAllTimerEvent();

    static void toLowerString(string &str);
    static void toUpperString(string &str);
    static int64_t str2ll(const string &str, int64_t errValue = 0);
    static std::string urlDecode(const std::string& input, bool isFormEncoded = false);
    static int hexToInt(char c);

private:
    volatile int _httpPort = -1;
    volatile int _httpsPort = -1;
    mg_connection *_httpServerConn = nullptr;
    mg_connection *_httpsServerConn = nullptr;

    struct mg_mgr *_mgr = nullptr;
    bool _selfMgrFlag = false;
    bool _selfThreadPoolFlag = false;
    string _certPath = "";
    string _keyPath = "";
    WHttpServerCbMsg _httpCbMsg;
    WHttpServerCbMsg _httpsCbMsg;
    std::map<int64_t, shared_ptr<HttpReqMsg>> _workingMsgMap;
    WThreadPool *_threadPool = nullptr;
    std::map<string, WHttpServerApiData> _httpApiMap;
    std::map<string, WHttpServerApiData> _chunkHttpApiMap;
    HttpFilterFun _httpFilterFun = nullptr;
    vector<WHttpStaticWebDir> _staticDirVect;
    std::atomic<int> _currentKeepAliveNum {0};
    uint64_t _currentTimerId = 1;
    std::map<uint64_t, WTimerData*> _timerEventMap;
    std::queue<WHttpLoopFun> _loopFunQueue;

    void recvHttpRequest(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
    void handleHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, WHttpServerApiData httpCbData);
    void handleChunkHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, WHttpServerApiData chunkHttpCbData);
    void sendHttpMsgPoll();
    void loopEventPoll();
    shared_ptr<string> deQueueHttpSendMsg(shared_ptr<HttpReqMsg> httpMsg);
    bool findHttpCbFun(mg_http_message *httpCbData, WHttpServerApiData &cbApiData);
    bool findChunkHttpCbFun(mg_http_message *httpCbData, WHttpServerApiData &cbApiData);
    bool isValidHttpChunk(mg_http_message *httpCbData);
    shared_ptr<HttpReqMsg> parseHttpMsg(struct mg_connection *conn, struct mg_http_message *httpCbData, bool chunkFlag = false);
    void enQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg, mg_http_message *httpCbData);
    void releaseHttpReqMsg(shared_ptr<HttpReqMsg> httpMsg);
    void closeHttpConnection(struct mg_connection *conn, bool isDirectClose = false);
    std::set<string> getSupportMethods(int httpMethods);
    bool handleStaticWebDir(shared_ptr<HttpReqMsg> httpMsg, WHttpStaticWebDir &webDir);
    void formStaticWebDirResHeader(stringstream &sstream, shared_ptr<HttpReqMsg> &httpMsg, WHttpStaticWebDir &webDir,
                       string &filePath, int code);
    void readStaticWebFile(shared_ptr<HttpReqMsg> httpMsg, FILE *file, int64_t contentLength,
                           int64_t startByte);
    void parseRangeStr(string rangeStr, int64_t &startByte, int64_t &endByte, int64_t fileSize);
    void reset();
    void logHttpRequestMsg(mg_connection *conn, mg_http_message *httpCbData);

    static void recvHttpRequestCallback(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
    static uint64_t getSysTickCountInMilliseconds();
    static void timerEventAdapter(void *ptr);
};



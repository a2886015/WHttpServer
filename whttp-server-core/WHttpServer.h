#pragma once

#include "IHttpServer.h"
#include <mutex>
#include <set>
#include <vector>
#include <time.h>
#include "WThreadPool.h"
#include <atomic>

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
struct HttpCbMsg
{
   WHttpServer *httpServer = nullptr;
   bool httpsFlag = false;
};

struct HttpApiData
{
    HttpCbFun httpCbFun = nullptr;
    int httpMethods = -1;
    bool findStaticFileFlag = false;
};

struct HttpStaticWebDir
{
    string dirPath = "";
    string header = "";
};

class WHttpServer: public IHttpServer
{
public:
    WHttpServer(mg_mgr *mgr = nullptr);
    virtual ~WHttpServer();
    virtual bool init(int maxEventThreadNum);
    virtual bool startHttp(int port);
    virtual bool startHttps(int port, string certPath, string keyPath);
    virtual bool stop();
    virtual bool run(int timeoutMs);
    virtual bool isRunning();
    virtual void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void setHttpFilter(HttpFilterFun filter);
    virtual void forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg);
    virtual void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, string *sendMsg);
    virtual string formJsonBody(int code, string message);
    virtual bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg);
    virtual shared_ptr<string> deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg);
    virtual bool addStaticWebDir(const string &dir, const string &header = "");

    static void toLowerString(string &str);
    static void toUpperString(string &str);

private:
    volatile int _httpPort = -1;
    volatile int _httpsPort = -1;
    std::mutex _httpLocker;
    struct mg_mgr *_mgr = nullptr;
    bool _selfMgrFlag = false;
    string _certPath = "";
    string _keyPath = "";
    HttpCbMsg _httpCbMsg;
    HttpCbMsg _httpsCbMsg;
    std::map<int64_t, shared_ptr<HttpReqMsg>> _workingMsgMap;
    WThreadPool *_threadPool = nullptr;
    std::map<string, HttpApiData> _httpApiMap;
    std::map<string, HttpApiData> _chunkHttpApiMap;
    HttpFilterFun _httpFilterFun = nullptr;
    vector<HttpStaticWebDir> _staticDirVect;
    std::atomic<int> _currentKeepAliveNum {0};

    void recvHttpRequest(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
    void handleHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, HttpApiData httpCbData);
    void handleChunkHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, HttpApiData chunkHttpCbData);
    void sendHttpMsgPoll();
    shared_ptr<string> deQueueHttpSendMsg(shared_ptr<HttpReqMsg> httpMsg);
    bool findHttpCbFun(mg_http_message *httpCbData, HttpApiData &cbApiData);
    bool findChunkHttpCbFun(mg_http_message *httpCbData, HttpApiData &cbApiData);
    bool isValidHttpChunk(mg_http_message *httpCbData);
    shared_ptr<HttpReqMsg> parseHttpMsg(struct mg_connection *conn, struct mg_http_message *httpCbData, bool chunkFlag = false);
    void enQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg, mg_http_message *httpCbData);
    void releaseHttpReqMsg(shared_ptr<HttpReqMsg> httpMsg);
    void closeHttpConnection(struct mg_connection *conn, bool isDirectClose = false);
    std::set<string> getSupportMethods(int httpMethods);
    bool handleStaticWebDir(shared_ptr<HttpReqMsg> httpMsg, HttpStaticWebDir &webDir);
    void formStaticWebDirResHeader(stringstream &sstream, shared_ptr<HttpReqMsg> &httpMsg, HttpStaticWebDir &webDir,
                       string &filePath, int code);
    void readStaticWebFile(shared_ptr<HttpReqMsg> httpMsg, FILE *file, int64_t contentLength,
                           int64_t startByte);
    void parseRangeStr(string rangeStr, int64_t &startByte, int64_t &endByte, int64_t fileSize);
    void reset();
    void logHttpRequestMsg(mg_connection *conn, mg_http_message *httpCbData);

    static void recvHttpRequestCallback(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
    static uint64_t getSysTickCountInMilliseconds();
};



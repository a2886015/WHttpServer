#pragma once

#include "IHttpServer.h"
#include <mutex>
#include <set>
#include "WThreadPool.h"

#define SEND_BUF_SIZE_BOUNDARY (3 * 1024 * 1024)

#define HTTP_UNKNOWN_REQUEST 100

class WHttpServer;
struct HttpCbMsg
{
   WHttpServer *httpServer;
   bool httpsFlag;
};

struct HttpApiData
{
    HttpCbFun httpCbFun;
    int httpMethods;
};

class WHttpServer: public IHttpServer
{
public:
    WHttpServer();
    virtual ~WHttpServer();
    virtual bool init(int maxEventThreadNum);
    virtual bool startHttp(int port);
    virtual bool startHttps(int port, string certPath, string keyPath);
    virtual bool stop();
    virtual bool run();
    virtual bool isRunning();
    virtual void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods);
    virtual void setHttpFilter(HttpFilterFun filter);
    virtual void closeHttpConnection(shared_ptr<HttpReqMsg> httpMsg, bool mainThread = false);
    virtual void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, string *sendMsg);
    virtual string formJsonBody(int code, string message);
    virtual bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg);

    static void toLowerString(string &str);
    static void toUpperString(string &str);

private:
    volatile int _httpPort = -1;
    volatile int _httpsPort = -1;
    std::mutex _httpLocker;
    struct mg_mgr _mgr;
    string _certPath = "";
    string _keyPath = "";
    HttpCbMsg _httpCbMsg;
    HttpCbMsg _httpsCbMsg;
    std::map<int64_t, shared_ptr<HttpReqMsg>> _workingMsgMap;
    WThreadPool *_threadPool = nullptr;
    std::map<string, HttpApiData> _httpApiMap;
    std::map<string, HttpApiData> _chunkHttpApiMap;
    HttpFilterFun _httpFilterFun = nullptr;

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
    void closeHttpConnection(struct mg_connection *conn, bool mainThread = false);
    std::set<string> getSupportMethods(int httpMethods);

    static void recvHttpRequestCallback(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
};



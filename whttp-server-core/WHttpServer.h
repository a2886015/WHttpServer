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
    virtual void addHttpApi(const string &uri, httpCbFun fun);
    virtual void addChunkHttpApi(const string &uri, httpCbFun fun);
    virtual void closeHttpConnection(shared_ptr<HttpReqMsg> httpMsg, bool mainThread = false);
    virtual void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body, bool closeFdFlag = false);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len);
    virtual void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, string *sendMsg);
    virtual string formJsonBody(int code, string message);

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
    std::map<string, httpCbFun> _httpApiMap;
    std::map<string, httpCbFun> _chunkHttpApiMap;

    void recvHttpRequest(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
    void sendHttpMsgPoll();
    shared_ptr<string> deQueueHttpSendMsg(shared_ptr<HttpReqMsg> httpMsg);
    bool findHttpCbFun(string &uri, httpCbFun &cbFun);
    bool findChunkHttpCbFun(mg_http_message *httpCbData, httpCbFun &cbFun);
    bool isValidHttpChunk(mg_http_message *httpCbData);
    shared_ptr<HttpReqMsg> parseHttpMsg(struct mg_connection *conn, struct mg_http_message *httpCbData, bool chunkFlag = false);
    void enQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg, mg_http_message *httpCbData);
    void handleHttpMsg(shared_ptr<HttpReqMsg> &httpMsg);
    void releaseHttpReqMsg(shared_ptr<HttpReqMsg> httpMsg);

    static void recvHttpRequestCallback(struct mg_connection *conn, int msgType, void *msgData, void *cbData);
};



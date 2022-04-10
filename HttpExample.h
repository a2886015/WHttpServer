#pragma once

#include "WHttpServer.h"

class HttpExample
{
public:
    HttpExample();
    virtual ~HttpExample();
    void start();
    void handleHttpRequestTest(shared_ptr<HttpReqMsg> &httpMsg);
    void handleHttpBigFileUpload(shared_ptr<HttpReqMsg> &httpMsg);
    void run();
private:
    WHttpServer *_httpServer = nullptr;
};


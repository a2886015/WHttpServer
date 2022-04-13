#pragma once

#include "WHttpServer.h"
#include <stdio.h>

class HttpExample
{
public:
    HttpExample();
    virtual ~HttpExample();
    void start();
    bool httpFilter(shared_ptr<HttpReqMsg> &httpMsg);
    void handleHttpRequestTest(shared_ptr<HttpReqMsg> &httpMsg);
    void handleHttpBigFileUpload(shared_ptr<HttpReqMsg> &httpMsg);
    void handleHttpDownloadFile(shared_ptr<HttpReqMsg> &httpMsg);
    void run();
private:
    WHttpServer *_httpServer = nullptr;
};


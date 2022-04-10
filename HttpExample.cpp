#include "HttpExample.h"

HttpExample::HttpExample()
{

}

HttpExample::~HttpExample()
{
    if (_httpServer)
    {
        delete _httpServer;
        _httpServer = nullptr;
    }
}

void HttpExample::start()
{
    _httpServer = new WHttpServer();
    _httpServer->init(32);
    _httpServer->startHttp(6200);

    httpCbFun testCbFun = std::bind(&HttpExample::handleHttpRequestTest, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/test", testCbFun);

    httpCbFun bigFileUploadCbFun = std::bind(&HttpExample::handleHttpBigFileUpload, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/bigfileupload", bigFileUploadCbFun);
}

void HttpExample::handleHttpRequestTest(shared_ptr<HttpReqMsg> &httpMsg)
{
    if (httpMsg->method != "GET")
    {
        _httpServer->httpReplyJson(httpMsg, 404, "", _httpServer->formJsonBody(HTTP_UNKNOWN_REQUEST, "do not support this method"), true);
        return;
    }
    _httpServer->httpReplyJson(httpMsg, 200, "", _httpServer->formJsonBody(0, "success"));
    _httpServer->closeHttpConnection(httpMsg);
}

void HttpExample::handleHttpBigFileUpload(shared_ptr<HttpReqMsg> &httpMsg)
{

}

void HttpExample::run()
{
    _httpServer->run();
}

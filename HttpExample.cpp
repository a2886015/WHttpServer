#include "HttpExample.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define HTTP_SEND_QUEUE_SIZE 3

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
    HttpFilterFun filterFun = std::bind(&HttpExample::httpFilter, this, std::placeholders::_1);
    _httpServer->setHttpFilter(filterFun);
    _httpServer->startHttp(6200);

    HttpCbFun normalCbFun = std::bind(&HttpExample::handleHttpRequestTest, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/test", normalCbFun);

    HttpCbFun bigFileUploadCbFun = std::bind(&HttpExample::handleHttpBigFileUpload, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/bigfileupload", bigFileUploadCbFun);

    HttpCbFun downloadFileCbFun = std::bind(&HttpExample::handleHttpDownloadFile, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/downloadFile/", downloadFileCbFun);
}

/*
 * http过滤函数，所有的请求都需要经过这个函数，若返回true，则可以进入回调，反之，无法进入回调，
 * 使用于需要有登录cookie信息的请求
*/
bool HttpExample::httpFilter(shared_ptr<HttpReqMsg> &httpMsg)
{
    return true;
}

void HttpExample::handleHttpRequestTest(shared_ptr<HttpReqMsg> &httpMsg)
{
    if (httpMsg->method != "GET")
    {
        _httpServer->httpReplyJson(httpMsg, 404, "", _httpServer->formJsonBody(HTTP_UNKNOWN_REQUEST, "do not support this method"));
        return;
    }
    // You can add http headers like below
    stringstream sstream;
    sstream << "Access-Control-Allow-Origin: *" << "\r\n";
    _httpServer->httpReplyJson(httpMsg, 200, sstream.str(), _httpServer->formJsonBody(0, "success"));
}

void HttpExample::handleHttpBigFileUpload(shared_ptr<HttpReqMsg> &httpMsg)
{

}

void HttpExample::handleHttpDownloadFile(shared_ptr<HttpReqMsg> &httpMsg)
{
    string fileName = httpMsg->uri.substr(strlen("/whttpserver/downloadFile/"));
    string filePath = "/data/" + fileName;
    FILE *file = fopen(filePath.c_str(), "r");
    if (!file)
    {
        Logw("handleHttpDownloadFile can not open file:%s", filePath.c_str());
        _httpServer->httpReplyJson(httpMsg, 500, "", _httpServer->formJsonBody(101, "can not open file"));
        return;
    }

    struct stat statbuf;
    stat(filePath.c_str(), &statbuf);
    int64_t fileSize = statbuf.st_size;

    int64_t currentReadSize = 0;
    int64_t maxPerReadSize = 1024*1024;
    int64_t perReadSize = fileSize > maxPerReadSize ? maxPerReadSize : fileSize;

    // 先返回http头部，文件下载数据返回量太大，需要分块返回，使用的是addSendMsgToQueue函数
    stringstream sstream;
    sstream << "HTTP/1.1 200 OK\r\n";
    sstream << "Content-Type: binary/octet-stream\r\n";
    sstream << "Content-Disposition: attachment;filename=" << fileName << "\r\n";
    sstream << "Content-Length: " << fileSize << "\r\n\r\n"; // 空行表示http头部完成
    _httpServer->addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());

    int64_t remainSize;
    int64_t currentMs = 0;

    while((remainSize = fileSize - currentReadSize) > 0 && _httpServer->isRunning())
    {
        if (_httpServer->isClientDisconnect(httpMsg))
        {
            Logw("handleHttpDownloadFile http client close the connection actively");
            break;
        }

        // 为了防止发送队列里的数据太大，占用大量内存，当发送队列里面的数据达到一定量，先等待
        if (httpMsg->sendQueue->size() >= HTTP_SEND_QUEUE_SIZE)
        {
            usleep(1000);
            continue;
        }

        string *fileStr = new string();
        fileStr->resize(perReadSize);

        int64_t currentWantReadSize = remainSize > perReadSize ? perReadSize : remainSize;
        int64_t readSize = fread((char *)fileStr->c_str(), 1, currentWantReadSize, file);
        // std::cout << "start byte is: " << startByte << ", currentReadSize is:" << currentReadSize + readSize << ", contentLength is: " << contentLength
        //           << ", offset is: " << currentReadSize + startByte << ", readSize is: " << readSize << endl;
        currentReadSize += readSize;
        if (readSize == 0)
        {
            Logw("handleHttpDownloadFile read size is 0");
            break;
        }

        if (readSize != perReadSize)
        {
            fileStr->resize(readSize);
        }

        // 再发送具体的文件数据给客户端
        _httpServer->addSendMsgToQueue(httpMsg, fileStr);
    }

}

void HttpExample::run()
{
    _httpServer->run();
}

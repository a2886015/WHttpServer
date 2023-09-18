#pragma once

#include "WHttpServer.h"
#include <stdio.h>

#define HTTP_OK 0
#define HTTP_UPLOAD_FAIL 106

#define MIN_FORM_DATA_PARSE_SIZE (100 * 1024)

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
    void handleHttpChunkDownloadFile(shared_ptr<HttpReqMsg> &httpMsg);
    void run(int timeoutMs);
private:
    WHttpServer *_httpServer = nullptr;

    bool parseMultipartStream(string &parseBuf, string &extraDataBuf, std::map<string, FILE *> &fileWriterMap,
                              std::map<string, string> &formParamsMap, string &filePathPrefix, string &errMsg);
    static string intToHexStr(int num);
};


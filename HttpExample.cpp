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
    HttpFilterFun filterFun = std::bind(&HttpExample::httpFilter, this, std::placeholders::_1);
    _httpServer->setHttpFilter(filterFun);

    stringstream sstream;
    sstream << "Access-Control-Allow-Origin: *" << "\r\n";
    sstream << "Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS" << "\r\n";
    sstream << "Access-Control-Allow-Headers: *" << "\r\n";
    _httpServer->addStaticWebDir("../web", sstream.str());
    // _httpServer->addStaticWebDir("/Users/kewen/working", sstream.str());
    // _httpServer->addStaticWebDir("/Users/kewen/Downloads/wawa", sstream.str());

    HttpCbFun normalCbFun = std::bind(&HttpExample::handleHttpRequestTest, this, std::placeholders::_1);
    _httpServer->addHttpApi("/whttpserver/test", normalCbFun, W_HTTP_GET);

    HttpCbFun bigFileUploadCbFun = std::bind(&HttpExample::handleHttpBigFileUpload, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/bigfileupload", bigFileUploadCbFun, W_HTTP_POST | W_HTTP_PUT);

    HttpCbFun downloadFileCbFun = std::bind(&HttpExample::handleHttpDownloadFile, this, std::placeholders::_1);
    _httpServer->addHttpApi("/whttpserver/downloadFile/", downloadFileCbFun, W_HTTP_GET | W_HTTP_HEAD);

    HttpCbFun chunkDownloadFileCbFun = std::bind(&HttpExample::handleHttpChunkDownloadFile, this, std::placeholders::_1);
    _httpServer->addHttpApi("/whttpserver/chunkDownloadFile/", chunkDownloadFileCbFun, W_HTTP_GET);

    TimerEventFun timerFun = std::bind(&HttpExample::timerEvent, this);
    _timerId = _httpServer->addTimerEvent(2000, timerFun);

    _httpServer->startHttp(6200);
    // _httpServer->startHttps(6443, "../cert/server.cert", "../cert/server.key");
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
    // You can add http headers like following code
    stringstream sstream;
    sstream << "Access-Control-Allow-Origin: *" << "\r\n";
    _httpServer->httpReplyJson(httpMsg, 200, sstream.str(), _httpServer->formJsonBody(0, "success"));
    // _httpServer->httpReplyJson(httpMsg, 200, "", _httpServer->formJsonBody(0, "success"));
}

void HttpExample::handleHttpBigFileUpload(shared_ptr<HttpReqMsg> &httpMsg)
{
    bool successFlag = true;
    string errMsg = "";

    string filePathPrefix = "/data/";

    string parseBuf = "";
    string extraDataBuf = "";

    std::map<string, FILE *> fileWriterMap;
    std::map<string, string> formParamsMap;

    while(!httpMsg->finishRecvChunk || (httpMsg->chunkQueue->size() > 0))
    {
        if (!_httpServer->isRunning())
        {
            WLogw("handleHttpBigFileUpload http server close");
            errMsg = "http server will close";
            successFlag = false;
            break;
        }

        if (_httpServer->isClientDisconnect(httpMsg))
        {
            WLogw("handleHttpBigFileUpload http client close the connection actively");
            successFlag = false;
            break;
        }

        shared_ptr<string> chunkData = _httpServer->deQueueHttpChunk(httpMsg);
        if (!chunkData.get())
        {
            this_thread::sleep_for(chrono::milliseconds(1));;
            continue;
        }

        parseBuf.append(extraDataBuf);
        extraDataBuf.clear();
        parseBuf.append(*chunkData);
        if (parseBuf.size() >= MIN_FORM_DATA_PARSE_SIZE)
        {
            successFlag = parseMultipartStream(parseBuf, extraDataBuf, fileWriterMap, formParamsMap, filePathPrefix, errMsg);
            if (!successFlag)
            {
                break;
            }
            parseBuf.clear();
        }
    }

    if (successFlag && !parseBuf.empty())
    {
        successFlag = parseMultipartStream(parseBuf, extraDataBuf, fileWriterMap, formParamsMap, filePathPrefix, errMsg);
    }

    WLogi("HttpServer::HandleFormDataUpload successFlag is %d, err msg is %s", successFlag, errMsg.c_str());

    for (auto it = fileWriterMap.begin(); it != fileWriterMap.end(); it++)
    {
        fclose(it ->second);
    }
    fileWriterMap.clear();

    if (successFlag)
    {
        _httpServer->httpReplyJson(httpMsg, 200, "", _httpServer->formJsonBody(HTTP_OK, "form-data upload success"));
    }
    else if (!successFlag && !errMsg.empty())
    {
        _httpServer->httpReplyJson(httpMsg, 500, "", _httpServer->formJsonBody(HTTP_UPLOAD_FAIL, errMsg));
    }
}

bool HttpExample::parseMultipartStream(string &parseBuf, string &extraDataBuf, std::map<string, FILE *> &fileWriterMap, std::map<string, string> &formParamsMap, string &filePathPrefix, string &errMsg)
{
    mg_http_part formInfo;
    struct mg_str body;
    body.ptr = (char *)parseBuf.c_str();
    body.len = parseBuf.size();
    int64_t offset = 0;
    struct mg_str head;
    struct mg_str extraData;
    bool partCompleted = false;
    do
    {
        /*
        * w_mg_http_next_multipart是我根据mongoose原生函数mg_http_next_multipart修改而来，专门用于解析流式http的
        * form-data表单上传数据
        */
        size_t parseLoc = w_mg_http_next_multipart(body, offset, &formInfo, &head, &extraData, &partCompleted);
        if (parseLoc == 0)
        {
            extraDataBuf.assign(&body.ptr[offset], body.len - offset);
        }
        else if (parseLoc > 0)
        {
            offset = parseLoc;
            string tempFileName = "";
            tempFileName.assign(formInfo.filename.ptr, formInfo.filename.len);
            string formKey = "";
            formKey.assign(formInfo.name.ptr, formInfo.name.len);
            if (!tempFileName.empty()) // form data file
            {
                if (fileWriterMap.find(tempFileName) == fileWriterMap.end())
                {
                    string filePath = filePathPrefix + tempFileName;
                    FILE *tmpWriter = fopen(filePath.c_str(), "w");
                    if (!tmpWriter)
                    {
                        errMsg = string("can not open file ") + filePath;
                        return false;
                    }

                    fwrite((char *)formInfo.body.ptr, 1, formInfo.body.len, tmpWriter);
                    fileWriterMap[tempFileName] = tmpWriter;
                }
                else
                {
                    FILE *tmpWriter  =  fileWriterMap[tempFileName];
                    fwrite((char *)formInfo.body.ptr, 1, formInfo.body.len, tmpWriter);
                }
            }
            else if (!formKey.empty()) // form data params
            {
                if (formParamsMap.find(formKey) == formParamsMap.end())
                {
                    string formValue = "";
                    formValue.append(formInfo.body.ptr, formInfo.body.len);
                    formParamsMap[formKey] = formValue;
                }
                else
                {
                    string &formValue = formParamsMap[formKey];
                    formValue.append(formInfo.body.ptr, formInfo.body.len);
                }
            }

            if (!partCompleted)
            {
                // save current header of form part
                extraDataBuf.assign(head.ptr, head.len);
                // add extra body data
                extraDataBuf.append(extraData.ptr, extraData.len);
            }
        }
    }
    while(partCompleted);
    return true;
}

void HttpExample::timerEvent()
{
    WLogi("HttpExample::timerEvent enter");
    _httpServer->deleteTimerEvent(_timerId);
}

string HttpExample::intToHexStr(int num)
{
    char data[50] = {0};
    sprintf(data, "%X", num); // x是小写，X是大写
    return data;
}

void HttpExample::handleHttpDownloadFile(shared_ptr<HttpReqMsg> &httpMsg)
{
    string fileName = httpMsg->uri.substr(strlen("/whttpserver/downloadFile/"));
    string filePath = "/data/" + fileName;

    FILE *file = fopen(filePath.c_str(), "r");
    if (!file)
    {
        WLogw("handleHttpDownloadFile can not open file:%s", filePath.c_str());
        _httpServer->httpReplyJson(httpMsg, 500, "", _httpServer->formJsonBody(101, "can not open file"));
        return;
    }

    struct stat statbuf;
    stat(filePath.c_str(), &statbuf);
    int64_t fileSize = statbuf.st_size;

    // 先返回http头部，文件下载数据返回量太大，需要分块返回，使用的是addSendMsgToQueue函数
    stringstream sstream;
    sstream << "HTTP/1.1 200 " << mg_http_status_code_str(200) << "\r\n";
    sstream << "Content-Type: binary/octet-stream\r\n";
    sstream << "Content-Disposition: attachment;filename=" << fileName << "\r\n";
    sstream << "Content-Length: " << fileSize << "\r\n\r\n"; // 空行表示http头部完成
    _httpServer->addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());

    if (httpMsg->method == "HEAD")
    {
        return;
    }

    int64_t currentReadSize = 0;
    int64_t maxPerReadSize = 1024*1024;
    int64_t perReadSize = fileSize > maxPerReadSize ? maxPerReadSize : fileSize;
    int64_t remainSize;

    while((remainSize = fileSize - currentReadSize) > 0 && _httpServer->isRunning())
    {
        if (_httpServer->isClientDisconnect(httpMsg))
        {
            WLogw("handleHttpDownloadFile http client close the connection actively");
            break;
        }

        // 为了防止发送队列里的数据太大，占用大量内存，当发送队列里面的数据达到一定量，先等待
        if (httpMsg->sendQueue->size() >= HTTP_SEND_QUEUE_SIZE)
        {
            this_thread::sleep_for(chrono::milliseconds(1));;
            continue;
        }

        // new了内存之后，外部不需要delete，httpServer内部会delete
        shared_ptr<string> fileStr = shared_ptr<string>(new string());
        fileStr->resize(perReadSize);

        int64_t currentWantReadSize = remainSize > perReadSize ? perReadSize : remainSize;
        int64_t readSize = fread((char *)fileStr->c_str(), 1, currentWantReadSize, file);
        // std::cout << "start byte is: " << startByte << ", currentReadSize is:" << currentReadSize + readSize << ", contentLength is: " << contentLength
        //           << ", offset is: " << currentReadSize + startByte << ", readSize is: " << readSize << endl;
        currentReadSize += readSize;
        if (readSize == 0)
        {
            WLogw("handleHttpDownloadFile read size is 0");
            break;
        }

        if (readSize != perReadSize)
        {
            fileStr->resize(readSize);
        }

        // 再发送具体的文件数据给客户端，fileStr的内存内部会delete
        _httpServer->addSendMsgToQueue(httpMsg, fileStr);
    }

    fclose(file);

}

void HttpExample::handleHttpChunkDownloadFile(shared_ptr<HttpReqMsg> &httpMsg)
{
    string fileName = httpMsg->uri.substr(strlen("/whttpserver/chunkDownloadFile/"));
    string filePath = "/data/" + fileName;

    FILE *file = fopen(filePath.c_str(), "r");
    if (!file)
    {
        WLogw("handleHttpDownloadFile can not open file:%s", filePath.c_str());
        _httpServer->httpReplyJson(httpMsg, 500, "", _httpServer->formJsonBody(101, "can not open file"));
        return;
    }

    struct stat statbuf;
    stat(filePath.c_str(), &statbuf);
    int64_t fileSize = statbuf.st_size;

    // 先返回http头部，文件下载数据返回量太大，需要分块返回，使用的是addSendMsgToQueue函数
    stringstream sstream;
    sstream << "HTTP/1.1 200 " << mg_http_status_code_str(200) << "\r\n";
    sstream << "Content-Type: " << guess_content_type(fileName.c_str()) << "\r\n";
    sstream << "Content-Disposition: attachment;filename=" << fileName << "\r\n";
    sstream << "Transfer-Encoding: chunked" << "\r\n"; // chunk下载头部需要添加这个字段，没有Content-Length
    sstream << "\r\n"; // 空行表示http头部完成
    _httpServer->addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());

    int64_t currentReadSize = 0;
    int64_t maxPerReadSize = 1024*1024;
    int64_t perReadSize = fileSize > maxPerReadSize ? maxPerReadSize : fileSize;
    int64_t remainSize;

    while((remainSize = fileSize - currentReadSize) > 0 && _httpServer->isRunning())
    {
        if (_httpServer->isClientDisconnect(httpMsg))
        {
            WLogw("handleHttpDownloadFile http client close the connection actively");
            break;
        }

        // 为了防止发送队列里的数据太大，占用大量内存，当发送队列里面的数据达到一定量，先等待
        if (httpMsg->sendQueue->size() >= HTTP_SEND_QUEUE_SIZE)
        {
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        string *fileStr = new string();
        fileStr->resize(perReadSize);

        int64_t currentWantReadSize = remainSize > perReadSize ? perReadSize : remainSize;
        int64_t readSize = fread((char *)fileStr->c_str(), 1, currentWantReadSize, file);
        std::cout << "currentReadSize is:" << currentReadSize << ", readSize is: " << readSize << endl;
        currentReadSize += readSize;
        if (readSize == 0)
        {
            WLogw("handleHttpDownloadFile read size is 0");
            delete fileStr;
            break;
        }

        if (readSize != perReadSize)
        {
            fileStr->resize(readSize);
        }

        sstream.clear();
        sstream.str("");
        sstream << intToHexStr(readSize) << "\r\n";
        sstream << (*fileStr) << "\r\n";
        // 再发送具体的文件数据给客户端
        _httpServer->addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());
        delete fileStr;
    }

    sstream.clear();
    sstream.str("");
    sstream << 0 << "\r\n";
    sstream << "\r\n";
    _httpServer->addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());

    fclose(file);
}

void HttpExample::run(int timeoutMs)
{
    _httpServer->run(timeoutMs);
}

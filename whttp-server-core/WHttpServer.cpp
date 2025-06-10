#include "WHttpServer.h"
#include <assert.h>

WHttpServer::WHttpServer(mg_mgr *mgr)
{
    _mgr = mgr;
    if (!_mgr)
    {
        _mgr = new mg_mgr();
        mg_mgr_init(_mgr);
        _selfMgrFlag = true;
    }
}

WHttpServer::~WHttpServer()
{
    deleteAllTimerEvent();
    stop();
    if (_selfMgrFlag)
    {
        mg_mgr_free(_mgr);
        delete _mgr;
        _mgr = nullptr;
    }

    if (_selfThreadPoolFlag && _threadPool)
    {
        _threadPool->waitForDone(3000);
        delete _threadPool;
        _threadPool = nullptr;
    }
}

bool WHttpServer::init(int maxEventThreadNum, WThreadPool *threadPool)
{
    _threadPool = threadPool;
    if (!_threadPool)
    {
        _selfThreadPoolFlag = true;
        _threadPool = new WThreadPool();
    }
    _threadPool->setMaxThreadNum(maxEventThreadNum);

    return true;
}

bool WHttpServer::startHttp(int port)
{
    if (!_threadPool)
    {
        WLogw("WHttpServer::StartHttp do not init");
        return false;
    }

    if (_httpPort != -1)
    {
        WLogw("WHttpServer::StartHttp http server is already start port:%d", _httpPort);
        return false;
    }
    std::stringstream sstream;
    sstream  << "http://0.0.0.0:" << port;
    _httpCbMsg.httpServer = this;
    _httpCbMsg.httpsFlag = false;
    _httpServerConn= mg_http_listen(_mgr, sstream.str().c_str(), WHttpServer::recvHttpRequestCallback, (void *)&_httpCbMsg);
    if (!_httpServerConn)
    {
        WLogw("WHttpServer::StartHttp http server start failed: %s", sstream.str().c_str());
        return false;
    }
    WLogi("WHttpServer::StartHttp http server start success: %s", sstream.str().c_str());
    _httpPort = port;
    return true;
}

bool WHttpServer::startHttps(int port, string certPath, string keyPath)
{
    if (!_threadPool)
    {
        WLogw("WHttpServer::StartHttps do not init");
        return false;
    }

    if (_httpsPort != -1)
    {
        WLogw("WHttpServer::StartHttps https server is already start port:%d", _httpsPort);
        return false;
    }
    _certPath = certPath;
    _keyPath = keyPath;
    std::stringstream sstream;
    sstream  << "https://0.0.0.0:" << port;
    _httpsCbMsg.httpServer = this;
    _httpsCbMsg.httpsFlag = true;
    _httpsServerConn = mg_http_listen(_mgr, sstream.str().c_str(), WHttpServer::recvHttpRequestCallback, (void *)&_httpsCbMsg);
    if (!_httpsServerConn)
    {
        WLogw("WHttpServer::StartHttps https server start failed: %s", sstream.str().c_str());
        return false;
    }
    WLogi("WHttpServer::StartHttps https server start success: %s", sstream.str().c_str());
    _httpsPort = port;
    return true;
}

bool WHttpServer::stop()
{
    if (_httpPort == -1 && _httpsPort == -1)
    {
        return true;
    }

    _httpPort = -1;
    _httpsPort = -1;

    if (_httpServerConn)
    {
        _httpServerConn->is_draining = 1;
    }

    if (_httpsServerConn)
    {
        _httpsServerConn->is_draining = 1;
    }

    this_thread::sleep_for(chrono::milliseconds(100)); // make sure other thread know server closing
    reset();
    return true;
}

bool WHttpServer::run(int timeoutMs)
{
    if (_httpPort != -1 || _httpsPort != -1)
    {
        sendHttpMsgPoll();
    }

    mg_mgr_poll(_mgr, timeoutMs);
    return true;
}

bool WHttpServer::isRunning()
{
    return (_httpPort != -1 || _httpsPort != -1);
}

void WHttpServer::addHttpApi(const string &uri, HttpCbFun fun, int httpMethods)
{
    WHttpServerApiData httpApiData;
    httpApiData.httpCbFun = fun;
    httpApiData.httpMethods = httpMethods;
    _httpApiMap[uri] = httpApiData;
}

void WHttpServer::addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods)
{
    WHttpServerApiData httpApiData;
    httpApiData.httpCbFun = fun;
    httpApiData.httpMethods = httpMethods;
    _chunkHttpApiMap[uri] = httpApiData;
}

void WHttpServer::setHttpFilter(HttpFilterFun filter)
{
    _httpFilterFun = filter;
}

void WHttpServer::forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg)
{
    mg_connection *conn = httpMsg->httpConnection;
    conn->is_closing = 1;
}

void WHttpServer::closeHttpConnection(struct mg_connection *conn, bool isDirectClose)
{
    if (conn->label[W_FD_STATUS_BIT] == HTTP_NORMAL_CLOSE)
    {
        return;
    }

    if (isDirectClose)
    {
        conn->is_draining = 1;
    }
    conn->label[W_FD_STATUS_BIT] = HTTP_NORMAL_CLOSE;
}

std::set<string> WHttpServer::getSupportMethods(int httpMethods)
{
    std::set<string> methodsSet;
    if (httpMethods & W_HTTP_GET)
    {
        methodsSet.insert("GET");
    }

    if (httpMethods & W_HTTP_GET)
    {
        methodsSet.insert("GET");
    }

    if (httpMethods & W_HTTP_POST)
    {
        methodsSet.insert("POST");
    }

    if (httpMethods & W_HTTP_PUT)
    {
        methodsSet.insert("PUT");
    }

    if (httpMethods & W_HTTP_DELETE)
    {
        methodsSet.insert("DELETE");
    }

    if (httpMethods & W_HTTP_HEAD)
    {
        methodsSet.insert("HEAD");
    }

    if (httpMethods & W_HTTP_OPTIONS)
    {
        methodsSet.insert("OPTIONS");
    }

    if (httpMethods & W_HTTP_PATCH)
    {
        methodsSet.insert("PATCH");
    }

    return methodsSet;
}

bool WHttpServer::handleStaticWebDir(shared_ptr<HttpReqMsg> httpMsg, WHttpStaticWebDir &webDir)
{
    string filePath = webDir.dirPath + httpMsg->uri;

    FILE *file = fopen(filePath.c_str(), "r");
    if (!file)
    {
        // Logw("WHttpServer::handleStaticWebDir can not open file:%s", filePath.c_str());
        // httpReplyJson(httpMsg, 400, "", formJsonBody(101, "can not find this file"));
        return false;
    }

    struct stat statbuf;
    stat(filePath.c_str(), &statbuf);
    int64_t fileSize = statbuf.st_size;
    stringstream sstream;

    if (httpMsg->method == "OPTIONS")
    {
        formStaticWebDirResHeader(sstream, httpMsg, webDir, filePath, 204);
        sstream << "\r\n"; // 空行表示http头部完成
        addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());
    }
    else
    {
        if (httpMsg->headers.find("range") != httpMsg->headers.end())
        {
            string rangeStr = httpMsg->headers["range"];
            int64_t startByte = 0, endByte = 0;
            parseRangeStr(rangeStr, startByte, endByte, fileSize);
            startByte = startByte < 0 ? 0 : startByte;
            endByte = (endByte > fileSize - 1) ? (fileSize - 1) : endByte;
            int64_t contentLength = endByte - startByte + 1;
            contentLength = contentLength < 0 ? 0 : contentLength;
            if (contentLength < fileSize)
            {
                formStaticWebDirResHeader(sstream, httpMsg, webDir, filePath, 206);
            }
            else
            {
                formStaticWebDirResHeader(sstream, httpMsg, webDir, filePath, 200);
            }
            sstream << "Content-Range: bytes " << startByte << "-" << endByte << "/" << fileSize << "\r\n";
            sstream << "Accept-Ranges: bytes\r\n";
            sstream << "Content-Length: " << contentLength << "\r\n";
            sstream << "\r\n";
            addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());
            if (httpMsg->method != "HEAD")
            {
                readStaticWebFile(httpMsg, file, contentLength, startByte);
            }
        }
        else
        {
            formStaticWebDirResHeader(sstream, httpMsg, webDir, filePath, 200);
            sstream << "Content-Length: " << fileSize << "\r\n";
            sstream << "\r\n";
            addSendMsgToQueue(httpMsg, sstream.str().c_str(), sstream.str().size());
            if (httpMsg->method != "HEAD")
            {
                readStaticWebFile(httpMsg, file, fileSize, 0);
            }
        }
    }

    fclose(file);
    if (httpMsg->isKeepingAlive)
    {
        httpMsg->lastKeepAliveTime = getSysTickCountInMilliseconds();
    }
    return true;
}

void WHttpServer::formStaticWebDirResHeader(stringstream &sstream, shared_ptr<HttpReqMsg> &httpMsg, WHttpStaticWebDir &webDir,
                                string &filePath, int code)
{
    sstream << "HTTP/1.1 "<< code << " " << mg_http_status_code_str(code) << "\r\n";
    sstream << "Content-Type: " << guess_content_type(filePath.c_str()) << "\r\n";
    map<string, string, WCaseCompare> &reqHeaders = httpMsg->headers;
    if (httpMsg->isKeepingAlive)
    {
        sstream << "Connection: " << "keep-alive" << "\r\n";
    }
    else if ((reqHeaders.find("connection") != reqHeaders.end()) && (reqHeaders["connection"] == "keep-alive"))
    {
        if (_currentKeepAliveNum < MAX_KEEP_ALIVE_NUM)
        {
            sstream << "Connection: " << "keep-alive" << "\r\n";
            _currentKeepAliveNum++;
            httpMsg->isKeepingAlive = true;
        }
        else
        {
            sstream << "Connection: " << "close" << "\r\n";
        }
    }

    if (!webDir.header.empty())
    {
        sstream << webDir.header;
    }
    // sstream << "Content-Disposition: attachment;filename=" << fileName << "\r\n";
}

void WHttpServer::readStaticWebFile(shared_ptr<HttpReqMsg> httpMsg, FILE *file, int64_t contentSize, int64_t startByte)
{
    int64_t currentReadSize = 0;
    int64_t maxPerReadSize = 1024*1024;
    int64_t perReadSize = contentSize > maxPerReadSize ? maxPerReadSize : contentSize;
    int64_t remainSize;

    uint64_t currentMs = 0;
    uint64_t lastWriteMs =  getSysTickCountInMilliseconds();
    fseek(file, startByte, SEEK_SET);
    while((remainSize = contentSize - currentReadSize) > 0 && isRunning())
    {
        if (isClientDisconnect(httpMsg))
        {
            WLogw("WHttpServer::readStaticWebFile http client close the connection actively");
            break;
        }

        // 为了防止发送队列里的数据太大，占用大量内存，当发送队列里面的数据达到一定量，先等待
        if (httpMsg->sendQueue->size() >= HTTP_SEND_QUEUE_SIZE)
        {
            currentMs = getSysTickCountInMilliseconds();
            if (currentMs - lastWriteMs > MAX_DOWNLOAD_PAUSE_TIME * 1000)
            {
                WLogi("WHttpServer::readStaticWebFile download file timeout %s", httpMsg->uri.c_str());
                forceCloseHttpConnection(httpMsg);
                return;
            }
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        shared_ptr<string> fileStr = shared_ptr<string>(new string());
        fileStr->resize(perReadSize);

        int64_t currentWantReadSize = remainSize > perReadSize ? perReadSize : remainSize;
        int64_t readSize = fread((char *)fileStr->c_str(), 1, currentWantReadSize, file);
        currentReadSize += readSize;
        if (readSize == 0)
        {
            WLogw("WHttpServer::readStaticWebFile read size is 0");
            break;
        }

        if (readSize != perReadSize)
        {
            fileStr->resize(readSize);
        }

        addSendMsgToQueue(httpMsg, fileStr);
        lastWriteMs =  getSysTickCountInMilliseconds();
    }
}

void WHttpServer::parseRangeStr(string rangeStr, int64_t &startByte, int64_t &endByte, int64_t fileSize)
{
    startByte = 0;
    endByte = 0;
    size_t equalMarkIndex = rangeStr.find('=');
    size_t lineMarkIndex = rangeStr.find('-');
    if (equalMarkIndex == string::npos || lineMarkIndex == string::npos)
    {
        return;
    }
    startByte = stoll(rangeStr.substr(equalMarkIndex + 1, lineMarkIndex - equalMarkIndex - 1));
    if (lineMarkIndex == rangeStr.size() - 1)
    {
        endByte = fileSize - 1;
    }
    else
    {
        endByte = stoll(rangeStr.substr(lineMarkIndex + 1));
    }
}

void WHttpServer::reset()
{
    _currentKeepAliveNum = 0;
}

void WHttpServer::logHttpRequestMsg(mg_connection *conn, mg_http_message *httpCbData)
{
    if (httpCbData->message.len < 1024)
    {
        WLogi("WHttpServer::logHttpRequestMsg %s request id:%ld, message: %s", conn->is_tls ? "https" : "http", conn->id, httpCbData->message.ptr);
    }
    else
    {
        char msg[1024] = {0};
        memcpy(msg, httpCbData->message.ptr, 1024);
        WLogi("WHttpServer::logHttpRequestMsg %s request id:%ld, pre 1024 message: %s", conn->is_tls ? "https" : "http", conn->id, msg);
    }
}

void WHttpServer::httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body)
{
    stringstream sstream;
    sstream << "HTTP/1.1 " << httpCode << " " << mg_http_status_code_str(httpCode) << "\r\n";
    sstream << "Content-Type: application/json\r\n";
    if (!head.empty())
    {
        sstream << head;
    }
    sstream << "Content-Length: " << body.size() << "\r\n\r\n";
    sstream << body;

    string data = sstream.str();
    addSendMsgToQueue(httpMsg, data.c_str(), data.size());
}

void WHttpServer::addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char *data, int len)
{
    shared_ptr<string> sendMsg = shared_ptr<string>(new string());
    sendMsg->resize(len);
    memcpy((char *)sendMsg->c_str(), data, len);
    bool res = httpMsg->sendQueue->enQueue(sendMsg);
    assert(res);
}

void WHttpServer::addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, shared_ptr<string> &sendMsg)
{
    bool res = httpMsg->sendQueue->enQueue(sendMsg);
    assert(res);
}

string WHttpServer::formJsonBody(int code, string message)
{
    stringstream sstream;
    sstream << "{";
    sstream << R"("code":)" << code << ",";
    sstream << R"("message":")" << message << R"(")";
    sstream << "}";
    return sstream.str();
}

bool WHttpServer::isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg)
{
    return (httpMsg->httpConnection->label[W_CLIENT_CLOSE_BIT] == 1);
}

shared_ptr<string> WHttpServer::deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg)
{
    shared_ptr<string> res = nullptr;
    httpMsg->chunkQueue->deQueue(res);
    return res;
}

bool WHttpServer::addStaticWebDir(const string &dir, const string &header)
{
    char tempDir[PATH_MAX];
    if (!realpath(dir.c_str(), tempDir))
    {
        WLoge("WHttpServer::addStaticWebDir the dir path is wrong: %s", dir.c_str());
        return false;
    }

    if (!mg_is_dir(tempDir))
    {
        WLoge("WHttpServer::addStaticWebDir is not dir: %s", dir.c_str());
        return false;
    }

    WHttpStaticWebDir staticDir;
    staticDir.dirPath = tempDir;
    staticDir.header = header;
    _staticDirVect.push_back(staticDir);
    return true;
}

uint64_t WHttpServer::addTimerEvent(unsigned long ms, TimerEventFun timerEventFun, WTimerRunType runType)
{
    uint16_t timerId = _currentTimerId++;
    WTimerData *timerData = new WTimerData();
    timerData->timerFun = timerEventFun;
    timerData->timeId = timerId;
    timerData->runType = runType;
    timerData->httpServer = this;
    _timerEventMap[timerId] = timerData;
    mg_timer_init(&timerData->timer, ms, MG_TIMER_REPEAT, &WHttpServer::timerEventAdapter, (void *)timerData);
    return timerId;
}

bool WHttpServer::deleteTimerEvent(uint64_t timerEventId)
{
    if (_timerEventMap.find(timerEventId) == _timerEventMap.end())
    {
        return true;
    }

    WTimerData *timerData = _timerEventMap[timerEventId];
    mg_timer_free(&timerData->timer);
    delete timerData;
    _timerEventMap.erase(timerEventId);
    return true;
}

bool WHttpServer::deleteAllTimerEvent()
{
    for(auto it = _timerEventMap.begin(); it != _timerEventMap.end(); )
    {
        WTimerData *timerData = it->second;
        mg_timer_free(&timerData->timer);
        delete timerData;
        it = _timerEventMap.erase(it);
    }

    return true;
}

void WHttpServer::recvHttpRequest(mg_connection *conn, int msgType, void *msgData, void *cbData)
{
    if (_httpPort == -1 && _httpsPort == -1)
    {
        return;
    }

    WHttpServerCbMsg *cbMsg = (WHttpServerCbMsg *)cbData;
    int64_t fd = (int64_t)conn->fd;
    if (msgType == MG_EV_ACCEPT && cbMsg->httpsFlag)
    {
        struct mg_tls_opts opts;
        opts.ca = nullptr;
        opts.cert = _certPath.c_str();
        opts.certkey = _keyPath.c_str();
        opts.ciphers = nullptr;
        opts.srvname.ptr = nullptr;
        opts.srvname.len = 0;
        WLogi("WHttpServer::recvHttpRequest https connect come id:%ld", conn->id);
        mg_tls_init(conn, &opts);
    }
    else if (msgType == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *httpCbData = (struct mg_http_message *) msgData;
        if (isValidHttpChunk(httpCbData))
        {
            return;
        }

        logHttpRequestMsg(conn, httpCbData);
        if (httpCbData->head.len > HTTP_MAX_HEAD_SIZE)
        {
            mg_http_reply(conn, 400, "", formJsonBody(HTTP_BEYOND_HEAD_SIZE, "head size beyond 2M").c_str());
            closeHttpConnection(conn, true);
            return;
        }

        WHttpServerApiData cbApiData;
        if (!findHttpCbFun(httpCbData, cbApiData))
        {
            if ((mg_vcasecmp(&(httpCbData->method), "GET") != 0) && (mg_vcasecmp(&(httpCbData->method), "HEAD") != 0)
                 && (mg_vcasecmp(&(httpCbData->method), "OPTIONS") != 0))
            {
                mg_http_reply(conn, 400, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "unknown request").c_str());
                closeHttpConnection(conn, true);
                return;
            }
            else
            {
                cbApiData.findStaticFileFlag = true;
            }
        }

        shared_ptr<HttpReqMsg> httpMsg = nullptr;
        // if keep-alive fd, erase last http msg
        if (_workingMsgMap.find(fd) != _workingMsgMap.end())
        {
            releaseHttpReqMsg(_workingMsgMap[fd]);
            _workingMsgMap.erase(fd);
            httpMsg = parseHttpMsg(conn, httpCbData);
            httpMsg->isKeepingAlive = true;
            _workingMsgMap[fd] = httpMsg;
        }
        else
        {
            httpMsg = parseHttpMsg(conn, httpCbData);
        }
        _workingMsgMap[fd] = httpMsg;
        _threadPool->concurrentRun(&WHttpServer::handleHttpMsg, this, std::ref(_workingMsgMap[fd]), cbApiData);
    }
    else if (msgType == MG_EV_HTTP_CHUNK)
    {
        struct mg_http_message *httpCbData = (struct mg_http_message *) msgData;
        WHttpServerApiData chunkCbApiData;
        if (!findChunkHttpCbFun(httpCbData, chunkCbApiData))
        {
            return;
        }

        if (httpCbData->head.len > HTTP_MAX_HEAD_SIZE)
        {
            mg_http_reply(conn, 400, "", formJsonBody(HTTP_BEYOND_HEAD_SIZE, "head size beyond 2M").c_str());
            closeHttpConnection(conn, true);
            return;
        }

        if (_workingMsgMap.find(fd) == _workingMsgMap.end())
        {
            logHttpRequestMsg(conn, httpCbData);
            shared_ptr<HttpReqMsg> httpMsg = parseHttpMsg(conn, httpCbData, true);
            _workingMsgMap[fd] = httpMsg;
            _threadPool->concurrentRun(&WHttpServer::handleChunkHttpMsg, this, std::ref(_workingMsgMap[fd]), chunkCbApiData);
        }
        else
        {
            shared_ptr<HttpReqMsg> httpMsg = _workingMsgMap[fd];
            enQueueHttpChunk(httpMsg, httpCbData);
        }
    }
    else if (msgType == MG_EV_CLOSE)
    {
        WLogi("WHttpServer::RecvHttpRequest http disconnect id:%ld", conn->id);
        if (conn->label[W_VALID_CONNECT_BIT] != 1)
        {
            return;
        }

        if (conn->label[W_FD_STATUS_BIT] == HTTP_NORMAL_CLOSE)
        {
            releaseHttpReqMsg(_workingMsgMap[fd]);
            _workingMsgMap.erase(fd);
            return;
        }

        conn->label[W_CLIENT_CLOSE_BIT] = 1;
        while(conn->label[W_FD_STATUS_BIT] == HTTP_IN_USE)
        {
            this_thread::sleep_for(chrono::microseconds(100));
        }
        releaseHttpReqMsg(_workingMsgMap[fd]);
        _workingMsgMap.erase(fd);
    }
    else if (msgType == MG_EV_ERROR)
    {
        char *errorStr = (char *) msgData;
        WLoge("WHttpServer::RecvHttpRequest mongoose error:%s", errorStr);
    }
}

void WHttpServer::handleHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, WHttpServerApiData httpCbData)
{
    if (httpCbData.findStaticFileFlag)
    {
        bool findFlag = false;
        for(int i = 0; i < (int)_staticDirVect.size(); i++)
        {
            if (handleStaticWebDir(httpMsg, _staticDirVect[i]))
            {
                findFlag = true;
                break;
            }
        }

        if (!findFlag)
        {
            httpReplyJson(httpMsg, 400, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "unknown request").c_str());
        }
    }
    else
    {
        if (_httpFilterFun && !_httpFilterFun(httpMsg))
        {
            closeHttpConnection(httpMsg->httpConnection);
            return;
        }

        set<string> methods = getSupportMethods(httpCbData.httpMethods);
        if (methods.find(httpMsg->method) == methods.end())
        {
            WLogw("WHttpServer::handleHttpMsg wrong http method: %s, uri: %s", httpMsg->method.c_str(), httpMsg->uri.c_str());
            httpReplyJson(httpMsg, 405, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "do not support this method"));
            closeHttpConnection(httpMsg->httpConnection);
            return;
        }
        httpCbData.httpCbFun(httpMsg);
    }

    if (httpMsg->isKeepingAlive)
    {
        httpMsg->httpConnection->label[W_FD_STATUS_BIT] = HTTP_NOT_USE;
    }
    else
    {
        closeHttpConnection(httpMsg->httpConnection);
    }
}

void WHttpServer::handleChunkHttpMsg(shared_ptr<HttpReqMsg> &httpMsg, WHttpServerApiData chunkHttpCbData)
{
    if (_httpFilterFun && !_httpFilterFun(httpMsg))
    {
        closeHttpConnection(httpMsg->httpConnection);
        return;
    }

    set<string> methods = getSupportMethods(chunkHttpCbData.httpMethods);
    if (methods.find(httpMsg->method) == methods.end())
    {
        httpReplyJson(httpMsg, 405, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "do not support this method"));
        closeHttpConnection(httpMsg->httpConnection);
        return;
    }

    chunkHttpCbData.httpCbFun(httpMsg);
    closeHttpConnection(httpMsg->httpConnection);
}

void WHttpServer::sendHttpMsgPoll()
{
    static int64_t pollCount = 0;
    pollCount++;
    int64_t currentTime = -1;
    if (pollCount % 1000 == 0)
    {
        currentTime = getSysTickCountInMilliseconds();
    }
    std::map<int64_t, shared_ptr<HttpReqMsg>>::iterator it;
    for (it = _workingMsgMap.begin(); it != _workingMsgMap.end(); it++)
    {
        shared_ptr<HttpReqMsg> httpMsg = it->second;
        mg_connection *conn = httpMsg->httpConnection;

        // identify if keep-alive timeout
        if (httpMsg->isKeepingAlive && (currentTime != -1) && (conn->label[W_FD_STATUS_BIT] == HTTP_NOT_USE))
        {
            if ((currentTime - httpMsg->lastKeepAliveTime > KEEP_ALIVE_TIME * 1000))
            {
                conn->label[W_FD_STATUS_BIT] = HTTP_NORMAL_CLOSE;
                _currentKeepAliveNum--;
            }
        }

        while ((httpMsg->sendQueue->size() > 0) && (conn->send.len < SEND_BUF_SIZE_BOUNDARY))
        {
            shared_ptr<string> sendMsg = deQueueHttpSendMsg(httpMsg);
            assert(sendMsg.get());
            mg_send(conn, (const void *)sendMsg->c_str(), sendMsg->size());
        }

        if ((conn->label[W_FD_STATUS_BIT] == HTTP_NORMAL_CLOSE) && (httpMsg->sendQueue->size() == 0))
        {
            conn->is_draining = 1;
            // continue;
        }
    }
}

shared_ptr<string> WHttpServer::deQueueHttpSendMsg(shared_ptr<HttpReqMsg> httpMsg)
{
    shared_ptr<string> sendMsg = nullptr;
    httpMsg->sendQueue->deQueue(sendMsg);
    return sendMsg;
}

bool WHttpServer::findChunkHttpCbFun(mg_http_message *httpCbData, WHttpServerApiData &cbApiData)
{
    bool res = false;
    for (auto it = _chunkHttpApiMap.begin(); it != _chunkHttpApiMap.end(); it++)
    {
        if (httpCbData->uri.len < it->first.size())
        {
            continue;
        }
        size_t cmpSize = it->first.size();
        if (strncmp(it->first.c_str(), httpCbData->uri.ptr, cmpSize) == 0)
        {
            if (((it->first)[cmpSize - 1] == '/') || (httpCbData->uri.len == cmpSize) ||
                    (httpCbData->uri.len > cmpSize && httpCbData->uri.ptr[cmpSize] == '/'))
            {
                cbApiData = it->second;
                res = true;
                break;
            }
        }
    }
    return res;
}

bool WHttpServer::isValidHttpChunk(mg_http_message *httpCbData)
{
    bool res = false;
    for (auto it = _chunkHttpApiMap.begin(); it != _chunkHttpApiMap.end(); it++)
    {
        if (httpCbData->uri.len < it->first.size())
        {
            continue;
        }
        size_t cmpSize = it->first.size();
        if (strncmp(it->first.c_str(), httpCbData->uri.ptr, cmpSize) == 0)
        {
            if (((it->first)[cmpSize - 1] == '/') || (httpCbData->uri.len == cmpSize) ||
                    (httpCbData->uri.len > cmpSize && httpCbData->uri.ptr[cmpSize] == '/'))
            {
                res = true;
                break;
            }
        }
    }
    return res;
}

bool WHttpServer::findHttpCbFun(mg_http_message *httpCbData, WHttpServerApiData &cbApiData)
{
    bool res = false;
    for (auto it = _httpApiMap.begin(); it != _httpApiMap.end(); it++)
    {
        if (httpCbData->uri.len < it->first.size())
        {
            continue;
        }

        size_t cmpSize = it->first.size();
        if (strncmp(it->first.c_str(), httpCbData->uri.ptr, cmpSize) == 0)
        {
            if (((it->first)[cmpSize - 1] == '/') || (httpCbData->uri.len == cmpSize) ||
                    (httpCbData->uri.len > cmpSize && httpCbData->uri.ptr[cmpSize] == '/'))
            {
                cbApiData = it->second;
                res = true;
                break;
            }
        }
    }
    return res;
}

shared_ptr<HttpReqMsg> WHttpServer::parseHttpMsg(mg_connection *conn, mg_http_message *httpCbData, bool chunkFlag)
{
    shared_ptr<HttpReqMsg> res = shared_ptr<HttpReqMsg>(new HttpReqMsg());
    res->httpConnection = conn;
    conn->label[W_VALID_CONNECT_BIT] = 1;
    conn->label[W_FD_STATUS_BIT] = HTTP_IN_USE;
    res->sendQueue = shared_ptr<HttpSendQueue>(new HttpSendQueue());

    res->method.resize(httpCbData->method.len);
    memcpy((char*)res->method.c_str(), httpCbData->method.ptr, httpCbData->method.len);
    toUpperString(res->method);

    res->uri.resize(httpCbData->uri.len);
    memcpy((char*)res->uri.c_str(), httpCbData->uri.ptr, httpCbData->uri.len);

    string queryKey = "";
    string queryValue = "";
    bool valueFlag = false;
    for (int i = 0; i < (int)httpCbData->query.len; i++)
    {
        if (httpCbData->query.ptr[i] == '=')
        {
            valueFlag = true;
            continue;
        }
        else if(httpCbData->query.ptr[i] == '&')
        {
            valueFlag = false;
            res->querys[queryKey] = queryValue;
            queryKey.clear();
            queryValue.clear();
            continue;
        }

        if (!valueFlag)
        {
            queryKey.append(1, httpCbData->query.ptr[i]);
        }
        else
        {
            queryValue.append(1, httpCbData->query.ptr[i]);
        }
    }
    res->querys[queryKey] = queryValue;

    res->proto.resize(httpCbData->proto.len);
    memcpy((char*)res->proto.c_str(), httpCbData->proto.ptr, httpCbData->proto.len);

    for(int i = 0; i < MG_MAX_HTTP_HEADERS; i++)
    {
        if (httpCbData->headers[i].name.len == 0 || !httpCbData->headers[i].name.ptr)
        {
            break;
        }
        string name;
        string value;
        name.resize(httpCbData->headers[i].name.len);
        value.resize(httpCbData->headers[i].value.len);
        memcpy((char*)name.c_str(), httpCbData->headers[i].name.ptr, httpCbData->headers[i].name.len);
        memcpy((char*)value.c_str(), httpCbData->headers[i].value.ptr, httpCbData->headers[i].value.len);
        res->headers[name] = value;
        // std::cout << "show headers, " << name << ": " << value << endl;
    }

    if (res->headers.find("content-length") != res->headers.end())
    {
        res->totalBodySize = (int64_t)stoll(res->headers["content-length"]);
    }
    else
    {
        WLogi("WHttpServer::ParseHttpMsg request id:%ld have no content-length", conn->id);
        res->totalBodySize = httpCbData->body.len;
    }

    if (chunkFlag)
    {
        res->chunkQueue = shared_ptr<HttpChunkQueue>(new HttpChunkQueue());
        shared_ptr<string> chunk = shared_ptr<string>(new string());
        chunk->resize(httpCbData->chunk.len);
        memcpy((char*)chunk->c_str(), httpCbData->chunk.ptr, httpCbData->chunk.len);
        conn->recv.len -= httpCbData->chunk.len;
        res->chunkQueue->enQueue(chunk);
        res->recvChunkSize += httpCbData->chunk.len;
        res->finishRecvChunk = (res->recvChunkSize >= res->totalBodySize);
    }
    else
    {
        res->body.resize(httpCbData->body.len);
        memcpy((char*)res->body.c_str(), httpCbData->body.ptr, httpCbData->body.len);
    }

    return res;
}

void WHttpServer::enQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg, mg_http_message *httpCbData)
{
    shared_ptr<string> chunk = shared_ptr<string>(new string());
    chunk->resize(httpCbData->chunk.len);
    memcpy((char*)chunk->c_str(), httpCbData->chunk.ptr, httpCbData->chunk.len);
    httpMsg->httpConnection->recv.len -= httpCbData->chunk.len;
    bool res = httpMsg->chunkQueue->enQueue(chunk);
    assert(res);
    if (httpMsg->chunkQueue->size() > CHUNK_QUEUE_SIZE_BOUNDARY)
    {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    httpMsg->recvChunkSize += httpCbData->chunk.len;
    httpMsg->finishRecvChunk = (httpMsg->recvChunkSize >= httpMsg->totalBodySize);
}

void WHttpServer::releaseHttpReqMsg(shared_ptr<HttpReqMsg> httpMsg)
{
    while (httpMsg->chunkQueue.get() && httpMsg->chunkQueue->size() > 0)
    {
        shared_ptr<string> res = nullptr;
        httpMsg->chunkQueue->deQueue(res);
    }

    while (httpMsg->sendQueue.get() && httpMsg->sendQueue->size() > 0)
    {
        shared_ptr<string> res = nullptr;
        httpMsg->sendQueue->deQueue(res);
    }
}

void WHttpServer::toLowerString(string &str)
{
    for(int i = 0; i < (int)str.size(); i++)
    {
        str[i] = tolower(str[i]);
    }
}

void WHttpServer::toUpperString(string &str)
{
    for(int i = 0; i < (int)str.size(); i++)
    {
        str[i] = toupper(str[i]);
    }
}

void WHttpServer::recvHttpRequestCallback(mg_connection *conn, int msgType, void *msgData, void *cbData)
{
    WHttpServerCbMsg *cbMsg = (WHttpServerCbMsg *)cbData;
    cbMsg->httpServer->recvHttpRequest(conn, msgType, msgData, cbData);
}

uint64_t WHttpServer::getSysTickCountInMilliseconds()
{
    timespec time;
    int ret = clock_gettime(CLOCK_MONOTONIC, &time);

    if (ret != 0)
    {
        printf("get clock error!\n");
    }
    uint64_t result = ((uint64_t)time.tv_sec) * 1000 + ((uint64_t)time.tv_nsec) / 1000000;
    return result;
}

void WHttpServer::timerEventAdapter(void *ptr)
{
    WTimerData *timerData = static_cast<WTimerData *>(ptr);
    (timerData->timerFun)();
    if (timerData->runType == WTimerRunOnce)
    {
        timerData->httpServer->deleteTimerEvent(timerData->timeId);
    }
}

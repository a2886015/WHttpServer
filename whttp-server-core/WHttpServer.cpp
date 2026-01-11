#include "WHttpServer.h"
#include <assert.h>
#include <cctype>
#include <iomanip>

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
    if (_httpServerConn)
    {
        _httpServerConn->fn = nullptr;
    }

    if (_httpsServerConn)
    {
        _httpsServerConn->fn = nullptr;
    }

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
        HLogw("WHttpServer::StartHttp do not init");
        return false;
    }

    if (_httpPort != -1)
    {
        HLogw("WHttpServer::StartHttp http server is already start port:%d", _httpPort);
        return false;
    }
    std::stringstream sstream;
    sstream  << "http://0.0.0.0:" << port;
    _httpCbMsg.httpServer = this;
    _httpCbMsg.httpsFlag = false;
    _httpServerConn= mg_http_listen(_mgr, sstream.str().c_str(), WHttpServer::recvHttpRequestCallback, (void *)&_httpCbMsg);
    if (!_httpServerConn)
    {
        HLogw("WHttpServer::StartHttp http server start failed: %s", sstream.str().c_str());
        return false;
    }
    HLogi("WHttpServer::StartHttp http server start success: %s", sstream.str().c_str());
    _httpPort = port;
    return true;
}

bool WHttpServer::startHttps(int port, string certPath, string keyPath)
{
    if (!_threadPool)
    {
        HLogw("WHttpServer::StartHttps do not init");
        return false;
    }

    if (_httpsPort != -1)
    {
        HLogw("WHttpServer::StartHttps https server is already start port:%d", _httpsPort);
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
        HLogw("WHttpServer::StartHttps https server start failed: %s", sstream.str().c_str());
        return false;
    }
    HLogi("WHttpServer::StartHttps https server start success: %s", sstream.str().c_str());
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
        _httpServerConn->is_closing = 1;
    }

    if (_httpsServerConn)
    {
        _httpsServerConn->is_closing = 1;
    }

    this_thread::sleep_for(chrono::milliseconds(100)); // make sure other thread know server closing
    reset();
    return true;
}

bool WHttpServer::run(int timeoutMs)
{
    if (_httpPort != -1 || _httpsPort != -1)
    {
        asyncCloseConnPoll();
        sendHttpMsgPoll();
    }

    loopEventPoll();
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
    conn->label[W_FD_STATUS_BIT] = HTTP_NORMAL_CLOSE;
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

    conn->label[W_FD_STATUS_BIT] = HTTP_NORMAL_CLOSE; // isDirectClose为false时，在sendHttpMsgPoll置位is_draining
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
    string filePath = webDir.dirPath + urlDecode(httpMsg->uri);

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
    // if (httpMsg->isKeepingAlive)
    // {
    //     httpMsg->lastKeepAliveTime = getSysTickCountInMilliseconds();
    // }
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
        if (setKeepAlive(httpMsg))
        {
            sstream << "Connection: " << "keep-alive" << "\r\n";
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

    // 有下面的header会强制浏览器下载文件，而web static目录要求浏览器可以解析的文件直接显示，而文件名浏览器从url中直接提取
    // sstream << R"(Content-Disposition: attachment; filename=")" << encodedFileName << R"("; filename*=utf-8'')" << encodedFileName << "\r\n";
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
            HLogw("WHttpServer::readStaticWebFile http client close the connection actively");
            break;
        }

        // 为了防止发送队列里的数据太大，占用大量内存，当发送队列里面的数据达到一定量，先等待
        if (httpMsg->sendQueue->size() >= HTTP_SEND_QUEUE_SIZE)
        {
            currentMs = getSysTickCountInMilliseconds();
            if (currentMs - lastWriteMs > MAX_DOWNLOAD_PAUSE_TIME * 1000)
            {
                HLogi("WHttpServer::readStaticWebFile download file timeout %s", httpMsg->uri.c_str());
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
            HLogw("WHttpServer::readStaticWebFile read size is 0");
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
    startByte = str2ll(rangeStr.substr(equalMarkIndex + 1, lineMarkIndex - equalMarkIndex - 1));
    if (lineMarkIndex == rangeStr.size() - 1)
    {
        endByte = fileSize - 1;
    }
    else
    {
        endByte = str2ll(rangeStr.substr(lineMarkIndex + 1));
    }
}

void WHttpServer::reset()
{
    _currentKeepAliveNum = 0;
}

void WHttpServer::logHttpRequestMsg(mg_connection *conn, mg_http_message *httpCbData)
{
    if (httpCbData->message.len < 2048)
    {
        HLogi("WHttpServer::logHttpRequestMsg %s request id:%ld, message: %s", conn->is_tls ? "https" : "http", conn->id, httpCbData->message.ptr);
    }
    else
    {
        char msg[2049] = {0};
        memcpy(msg, httpCbData->message.ptr, 2048);
        msg[2048] = '\0';
        HLogi("WHttpServer::logHttpRequestMsg %s request id:%ld, pre 2048 message: %s", conn->is_tls ? "https" : "http", conn->id, msg);
    }
}

void WHttpServer::handleHttpReplyWhenAbnormal(mg_connection *conn, int httpCode, string head, string body)
{
    shared_ptr<HttpReqMsg> httpMsg = nullptr;
    int64_t fd = (int64_t)conn->fd;
    // if keep-alive fd, erase last http msg
    if (_workingMsgMap.find(fd) != _workingMsgMap.end())
    {
        httpMsg = _workingMsgMap[fd];
        if (httpMsg->isKeepingAlive)
        {
            releaseHttpReqMsg(_workingMsgMap[fd]);
            _workingMsgMap.erase(fd);
            httpMsg = parseHttpMsgWhenAbnormal(conn);
            httpMsg->isKeepingAlive = false;
            _currentKeepAliveNum--;
        }
        else
        {
            // 这里考虑到上轮任务的线程可能还没有退出，所以用httpReplyJson，至于socket的关闭，依靠上一轮的释放就行，httpMsg也用上一轮的
            httpReplyJson(httpMsg, 400, "", formJsonBody(HTTP_NOT_KEEP_ALIVE, "do not support keep alive"));
            return;
        }
    }
    else
    {
        httpMsg = parseHttpMsgWhenAbnormal(conn);
    }

    _workingMsgMap[fd] = httpMsg;
    httpReplyJson(httpMsg, httpCode, head, body);
    closeHttpConnection(conn);
}

void WHttpServer::httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body)
{
    stringstream sstream;
    sstream << "HTTP/1.1 " << httpCode << " " << mg_http_status_code_str(httpCode) << "\r\n";
    sstream << "Content-Type: application/json\r\n";
    if (httpMsg->isKeepingAlive)
    {
        sstream << "Connection: keep-alive\r\n";
    }
    else
    {
        sstream << "Connection: close\r\n";
    }

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
        HLoge("WHttpServer::addStaticWebDir the dir path is wrong: %s", dir.c_str());
        return false;
    }

    if (!mg_is_dir(tempDir))
    {
        HLoge("WHttpServer::addStaticWebDir is not dir: %s", dir.c_str());
        return false;
    }

    WHttpStaticWebDir staticDir;
    staticDir.dirPath = tempDir;
    staticDir.header = header;
    _staticDirVect.push_back(staticDir);
    return true;
}

uint64_t WHttpServer::addTimerEvent(unsigned long ms, WTimerEventFun timerEventFun, WTimerRunType runType)
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
    _timerEventMap.erase(timerEventId);
    delete timerData;
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

void WHttpServer::addNextLoopFun(WHttpNextLoopFun fun)
{
    _loopFunQueue.enQueue(fun);
}

bool WHttpServer::setKeepAlive(shared_ptr<HttpReqMsg> httpMsg)
{
    if (_currentKeepAliveNum < MAX_KEEP_ALIVE_NUM)
    {
        _currentKeepAliveNum++;
        httpMsg->isKeepingAlive = true;
        return true;
    }
    else
    {
        HLogw("WHttpServer::setKeepAlive reach the max num");
        return false;
    }
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
        HLogi("WHttpServer::recvHttpRequest https connect come id:%ld", conn->id);
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
            handleHttpReplyWhenAbnormal(conn, 400, "", formJsonBody(HTTP_BEYOND_HEAD_SIZE, "head size beyond 2M"));
            // mg_http_reply(conn, 400, "", formJsonBody(HTTP_BEYOND_HEAD_SIZE, "head size beyond 2M").c_str());
            // closeHttpConnection(conn, true);
            return;
        }

        WHttpServerApiData cbApiData;
        if (!findHttpCbFun(httpCbData, cbApiData))
        {
            if ((mg_vcasecmp(&(httpCbData->method), "GET") != 0) && (mg_vcasecmp(&(httpCbData->method), "HEAD") != 0)
                    && (mg_vcasecmp(&(httpCbData->method), "OPTIONS") != 0))
            {
                handleHttpReplyWhenAbnormal(conn, 400, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "unknown request"));
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
            httpMsg = _workingMsgMap[fd];
            if (httpMsg->isKeepingAlive)
            {
                releaseHttpReqMsg(_workingMsgMap[fd]);
                _workingMsgMap.erase(fd);
                httpMsg = parseHttpMsg(conn, httpCbData);
                httpMsg->isKeepingAlive = true;
            }
            else
            {
                // 这里考虑到上轮任务的线程可能还没有退出，所以用httpReplyJson，至于socket的关闭，依靠上一轮的释放就行，httpMsg也用上一轮的
                httpReplyJson(httpMsg, 400, "", formJsonBody(HTTP_NOT_KEEP_ALIVE, "do not support keep alive"));
                return;
            }
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
            handleHttpReplyWhenAbnormal(conn, 400, "", formJsonBody(HTTP_BEYOND_HEAD_SIZE, "head size beyond 2M"));
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
        HLogi("WHttpServer::RecvHttpRequest http disconnect id:%ld", conn->id);
        if (conn->label[W_VALID_CONNECT_BIT] != 1)
        {
            return;
        }

        if (conn->label[W_FD_STATUS_BIT] == HTTP_NORMAL_CLOSE)
        {
            w_close_conn(conn);
            releaseHttpReqMsg(_workingMsgMap[fd]);
            _workingMsgMap.erase(fd);
            return;
        }

        conn->label[W_CLIENT_CLOSE_BIT] = 1;
        _clientSelfCloseList.push_back(conn);
    }
    else if (msgType == MG_EV_ERROR)
    {
        char *errorStr = (char *) msgData;
        HLoge("WHttpServer::RecvHttpRequest mongoose error:%s", errorStr);
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
            HLogw("WHttpServer::handleHttpMsg wrong http method: %s, uri: %s", httpMsg->method.c_str(), httpMsg->uri.c_str());
            httpReplyJson(httpMsg, 405, "", formJsonBody(HTTP_UNKNOWN_REQUEST, "do not support this method"));
            closeHttpConnection(httpMsg->httpConnection);
            return;
        }
        httpCbData.httpCbFun(httpMsg);
    }

    if (httpMsg->isKeepingAlive)
    {
        httpMsg->httpConnection->label[W_FD_STATUS_BIT] = HTTP_NOT_USE;
        httpMsg->lastKeepAliveTime = getSysTickCountInMilliseconds();
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

void WHttpServer::asyncCloseConnPoll()
{
    for (auto it = _clientSelfCloseList.begin(); it != _clientSelfCloseList.end();)
    {
        mg_connection *conn = *it;
        if (conn->label[W_FD_STATUS_BIT] == HTTP_IN_USE)
        {
            ++it;
        }
        else
        {
            HLogi("WHttpServer::asyncCloseConnPoll close conn id: %ld", conn->id);
            int64_t fd = (int64_t)conn->fd;
            w_close_conn(conn);
            releaseHttpReqMsg(_workingMsgMap[fd]);
            _workingMsgMap.erase(fd);
            it = _clientSelfCloseList.erase(it);
        }
    }
}

void WHttpServer::sendHttpMsgPoll()
{
    _pollCount++;
    int64_t currentTime = -1;
    if (_pollCount % 1000 == 0)
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

void WHttpServer::loopEventPoll()
{
    while(!_loopFunQueue.empty())
    {
        WHttpNextLoopFun loopFun;
        _loopFunQueue.deQueue(loopFun);
        loopFun();
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
    conn->label[W_EXTERNAL_CLOSE_BIT] = 1;
    conn->label[W_FD_STATUS_BIT] = HTTP_IN_USE;
    res->sendQueue = shared_ptr<HttpSendQueue>(new HttpSendQueue());

    res->method.resize(httpCbData->method.len);
    memcpy((char*)res->method.c_str(), httpCbData->method.ptr, httpCbData->method.len);
    toUpperString(res->method);

    res->uri.resize(httpCbData->uri.len);
    memcpy((char*)res->uri.c_str(), httpCbData->uri.ptr, httpCbData->uri.len);

    parseHttpQuery(httpCbData, res->querys);

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
        res->totalBodySize = str2ll(res->headers["content-length"]);
    }
    else
    {
        HLogi("WHttpServer::ParseHttpMsg request id:%ld have no content-length", conn->id);
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

shared_ptr<HttpReqMsg> WHttpServer::parseHttpMsgWhenAbnormal(mg_connection *conn)
{
    shared_ptr<HttpReqMsg> res = shared_ptr<HttpReqMsg>(new HttpReqMsg());
    res->httpConnection = conn;
    conn->label[W_VALID_CONNECT_BIT] = 1;
    conn->label[W_EXTERNAL_CLOSE_BIT] = 1;
    conn->label[W_FD_STATUS_BIT] = HTTP_IN_USE;
    res->sendQueue = shared_ptr<HttpSendQueue>(new HttpSendQueue());
    return res;
}

void WHttpServer::parseHttpQuery(mg_http_message *httpCbData, std::map<std::string, std::string> &queryMap)
{
    if (!httpCbData || (httpCbData->query.len == 0) || !httpCbData->query.ptr) {
        return;
    }

    // 直接使用原始指针和长度，避免中间字符串拼接
    const char* start = httpCbData->query.ptr;
    const char* end = start + httpCbData->query.len;
    const char* keyBegin = start;
    const char* valBegin = nullptr;

    for (const char* p = start; p < end; ++p) {
        if (*p == '=')
        {
            if (valBegin == nullptr) // 只处理第一个'='，忽略后续无效的'='
            {
                valBegin = p + 1;
            }
        }
        else if (*p == '&')
        {
            std::string key;
            std::string value;

            if (valBegin == nullptr) { // 没有'='的情况，视为key，value为空
                key = urlDecode(std::string(keyBegin, p - keyBegin));
            } else {
                key = urlDecode(std::string(keyBegin, valBegin - keyBegin - 1));
                value = urlDecode(std::string(valBegin, p - valBegin));
            }

            queryMap.emplace(std::move(key), std::move(value));

            // 重置指针，准备下一组键值对
            keyBegin = p + 1;
            valBegin = nullptr;
        }
    }

    // 处理最后一组键值对（循环结束后剩余的内容）
    if (keyBegin < end) {
        std::string key;
        std::string value;

        if (valBegin == nullptr) {
            key = urlDecode(std::string(keyBegin, end - keyBegin));
        } else {
            key = urlDecode(std::string(keyBegin, valBegin - keyBegin - 1));
            value = urlDecode(std::string(valBegin, end - valBegin));
        }

        queryMap.emplace(std::move(key), std::move(value));
    }
}

void WHttpServer::enQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg, mg_http_message *httpCbData)
{
    /*
     * 由于我对mongoose源码1571行左右的修改，导致最后进入MG_EV_HTTP_MSG时，也会额外触发MG_EV_HTTP_CHUNK，
     * 这次chunk.len为0，最好不要让上层感知这个，所以添加这个条件
     */
    if (httpCbData->chunk.len == 0)
    {
        return;
    }

    shared_ptr<string> chunk = shared_ptr<string>(new string());
    chunk->resize(httpCbData->chunk.len);
    memcpy((char*)chunk->c_str(), httpCbData->chunk.ptr, httpCbData->chunk.len);
    // mg_iobuf_delete(&httpMsg->httpConnection->recv, httpMsg->httpConnection->recv.len);
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

int64_t WHttpServer::str2ll(const string &str, int64_t errValue)
{
    try {
        return std::stoll(str);
    } catch (const std::exception& e) {
        HLogw("WHttpServer::str2ll error: %s", e.what());
        return errValue;
    }
}

string WHttpServer::urlDecode(const string &input, bool isFormEncoded)
{
    std::string result;
    size_t i = 0;
    const size_t len = input.length();

    while (i < len) {
        if (input[i] == '%') {
            if (i + 2 >= len) {  // 检查是否有足够的字符进行解码
                return "";
            }

            // 解析两个十六进制字符
            int hex1 = hexToInt(input[i+1]);
            int hex2 = hexToInt(input[i+2]);
            if (hex1 == -1 || hex2 == -1) {
                return ""; // 无效的十六进制字符，解码失败
            }

            result += static_cast<char>((hex1 << 4) | hex2);
            i += 3; // 跳过%XX三个字符
        }
        else if (isFormEncoded && input[i] == '+') { // 表单模式下，+号转为空格
            result += ' ';
            i++;
        }
        else { // 检查是否有足够的字符进行解码
            result += input[i];
            i++;
        }
    }

    return result;
}

string WHttpServer::urlEncode(const string &input, bool isFormEncoded)
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase;

    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        }
        else if (c == ' ') { // 空格处理：表单模式下转为+，否则转为%20
            oss << (isFormEncoded ? "+" : "%20");
        }
        else {  // 其他字符按UTF-8字节编码为%XX
            oss << "%" << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }

    return oss.str();
}

int WHttpServer::hexToInt(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int64_t WHttpServer::getIntFromQuery(const map<string, string> &querys, const string &key, int64_t defaultValue)
{
    if (querys.find(key) != querys.end()) {
        std::string str = querys.at(key);

        try {
            return std::stoll(str);
        } catch (std::exception& e) {
            HLogw("WHttpServer::getIntFromQuery error");
            return defaultValue;
        }
    } else {
        return defaultValue;
    }
}

string WHttpServer::getStrFromQuery(const map<string, string> &querys, const string &key, const string &defaultValue)
{
    if (querys.find(key) != querys.end()) {
        return querys.at(key);
    } else {
        return defaultValue;
    }
}

void WHttpServer::recvHttpRequestCallback(mg_connection *conn, int msgType, void *msgData, void *cbData)
{
    WHttpServerCbMsg *cbMsg = (WHttpServerCbMsg *)cbData;
    cbMsg->httpServer->recvHttpRequest(conn, msgType, msgData, cbData);
}

uint64_t WHttpServer::getSysTickCountInMilliseconds()
{
    timespec time2;
    int ret = clock_gettime(CLOCK_MONOTONIC, &time2);

    if (ret != 0)
    {
        HLogw("get clock error!");
    }
    uint64_t result = ((uint64_t)time2.tv_sec) * 1000 + ((uint64_t)time2.tv_nsec) / 1000000;
    return result;
}

void WHttpServer::timerEventAdapter(void *ptr)
{
    WTimerData *timerData = static_cast<WTimerData *>(ptr);
    (timerData->timerFun)();
    if (timerData->runType == WTimerRunOnce)
    {
        WHttpNextLoopFun loopFun = std::bind(&WHttpServer::deleteTimerEvent, timerData->httpServer, timerData->timeId);
        timerData->httpServer->addNextLoopFun(loopFun);
    }
}

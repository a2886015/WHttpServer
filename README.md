# WHttpServer

#### 介绍
基于mongoose 7.3版本的源码，修改源码及二次封装，引入线程池编写的http服务，同时支持https。
使用示例可以查看HttpExample.cpp、HttpExample.h和main.cpp三个文件，里面分别举例了http普通接口、大文件上传、大文件下载、chunk流文件下载4个典型场景。

操作系统：Linux。
在mac下也可以运行，但是需要自己修改CMakeLists.txt文件适配好openssl的库；

另外，在linux系统下，可以将CMakeLists.txt中add_definitions(-DUSE_EPOLL)这句打开，这样底层就会切换成epoll

#### 安装教程

直接将whttp-server-core目录里面的文件拷贝到自己的工程即可

#### 接口说明

1. bool init(int maxEventThreadNum)，初始化线程池，指定线程池最大线程数
2. bool startHttp(int port)，开启http服务
3. bool startHttps(int port, string certPath, string keyPath)，开启https服务
4. bool stop()，停止http和https服务，在析构函数函数里面已经调用
5. bool run(int timeoutMs)，服务运行的发动机，外部必须用一个死循环一直调用该函数
6. bool isRunning()，查看服务是否还在运行中
7. void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods)，添加普通的http回调接口，其中httpMethods以数据的不同位置位代表不同的http方法，具体如下：

         #define W_HTTP_GET      (1 << 0)
         #define W_HTTP_POST     (1 << 1)
         #define W_HTTP_PUT      (1 << 2)
         #define W_HTTP_DELETE   (1 << 3)
         #define W_HTTP_HEAD     (1 << 4)
         #define W_HTTP_OPTIONS  (1 << 5)
         #define W_HTTP_ALL       0xFF

8. void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods)，添加数据块http回调接口，当客户端的http请求数据可能超过3M时，采用这个函数添加接口，典型的场景如文件上传
9. void setHttpFilter(HttpFilterFun filter)，设置http接口的过滤函数，若过滤函数filter返回false，则不会进入回调，用于需要如登录信息才会响应的接口
10. void forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg)，用于强行关闭socket连接，正常情况下，回调完成后，框架内部会自己关闭socket连接。但是有些情况下，比如文件下载时，客户端暂停了下载，而且暂停时间很久，这就导致服务器的某个线程的任务一直被卡主，不得不强制关闭socket
11. void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len)，向客户端返回数据，会先放入缓冲区，等待下次循环时再发送出去，外部需要自己管理date的内存
12. void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, shared_ptr<string> &sendMsg)，向客户端返回数据，上面函数的重载
13. void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body)，向客户端返回数据，返回type是json，这是对上面2个函数进一步封装的便捷函数
14. string formJsonBody(int code, string message)，返回类似{"code":0, "message": "success"}的json字符串
15. bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg)，返回客户端是否主动断开了连接
16. shared_ptr<string> deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg)，大文件上传的场景，获取chunk数据块
17. bool addStaticWebDir(const string &dir, const string &header = "")，新增接口，用于形成web容器目录，可部署网页
18. mg_http_status_code_str(int status_code), mongoose原生接口，返回http status code对应的string信息



#### 重要数据类型

```
1. using HttpCbFun = std::function<void(shared_ptr<HttpReqMsg> &)>，http接口的回调函数
2. using HttpFilterFun = std::function<bool(shared_ptr<HttpReqMsg> &)>，http接口的过滤函数
3. HttpReqMsg结构体

 struct HttpReqMsg
{
   mg_connection *httpConnection = nullptr; // mongoose里面代表一个socket连接的结果体，外层不关心
   string method; // http方法，可以是GET POST PUT DELETE等
   string uri; 
   map<string, string> querys; // the params in uri
   string proto; // http version
   map<string, string, WCaseCompare> headers; // http头部字段，其中key是大小写不敏感的，比如"Content-Length" 和 "content-length"是一个效果
   string body; // http body体数据
   int64_t totalBodySize; // http body体数据大小，就是头部"content-length"值
   shared_ptr<HttpChunkQueue> chunkQueue; // 大文件上传时，存储文件的数据块
   shared_ptr<HttpSendQueue> sendQueue; // http返回给客户端的数据队列
   int64_t recvChunkSize = 0; // 大文件上传时，已经接收到的数据块大小
   bool finishRecvChunk = false; // 大文件上传时，判断是否所有数据都上传完成了
};
```

#### 注意事项
1、所有http回调函数都是在子线程里面运行的，即使同一个回调函数，每次运行也不一定在一个线程，注意线程安全

2、为了保证更好的性能，发动机函数run里面没有加锁，不是线程安全的，所以初始化之类的函数，如init、startHttp、addHttpApi之类的函数的调用需要在启动run函数之前运行。

3、addHttpApi和addChunkHttpApi函数给uri时，不要给可能重复匹配的uri，否则只有1个生效。比如给“/test”和”/test/dotest“这样的，因为当uri是”/test/dotest“时，“/test”也是可以匹配成功，我认为这种情况属于uri没有设计好，不想像node.js一样提供next函数处理这种情况。若你uri直接给‘/’，那么估计所有的回调全都会进‘/’的回调函数中

#### 示例代码

初始化代码：

```
void HttpExample::start()
{
    _httpServer = new WHttpServer();
    _httpServer->init(32);
    HttpFilterFun filterFun = std::bind(&HttpExample::httpFilter, this, std::placeholders::_1);
    _httpServer->setHttpFilter(filterFun);
    _httpServer->startHttp(6200);

    HttpCbFun normalCbFun = std::bind(&HttpExample::handleHttpRequestTest, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/test", normalCbFun, W_HTTP_GET);

    HttpCbFun bigFileUploadCbFun = std::bind(&HttpExample::handleHttpBigFileUpload, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/bigfileupload", bigFileUploadCbFun, W_HTTP_POST | W_HTTP_PUT);

    HttpCbFun downloadFileCbFun = std::bind(&HttpExample::handleHttpDownloadFile, this, std::placeholders::_1);
    _httpServer->addChunkHttpApi("/whttpserver/downloadFile/", downloadFileCbFun, W_HTTP_GET | W_HTTP_HEAD);
}
```




在main函数中启动发动机：


```
while(true)
{
    httpTest.run(2);
}
```



http接口回调示例代码：

```
void HttpExample::handleHttpRequestTest(shared_ptr<HttpReqMsg> &httpMsg)
{
    // You can add http headers like below
    stringstream sstream;
    sstream << "Access-Control-Allow-Origin: *" << "\r\n";
    _httpServer->httpReplyJson(httpMsg, 200, sstream.str(), _httpServer->formJsonBody(0, "success"));
    // _httpServer->httpReplyJson(httpMsg, 200, "", _httpServer->formJsonBody(0, "success"));
}
```



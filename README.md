# WHttpServer

#### 介绍
基于mongoose 7.3版本的源码，修改源码及二次封装，引入线程池编写的http服务，同时支持https
使用示例可以查看HttpExample.cpp、HttpExample.h和main.cpp三个文件，里面分别举例了http普通接口、大文件上传、大文件下载3个典型场景

#### 安装教程

直接将whttp-server-core目录里面的文件拷贝到自己的工程即可

#### 接口说明

1. bool init(int maxEventThreadNum)，初始化线程池，指定线程池最大线程数
2. bool startHttp(int port)，开启http服务
3. bool startHttps(int port, string certPath, string keyPath)，开启https服务
4. bool stop()，停止http和https服务，在析构函数函数里面已经调用
5. bool run()，服务运行的发动机，外部必须用一个死循环一直调用该函数
6. bool isRunning()，查看服务是否还在运行中
7. void addHttpApi(const string &uri, HttpCbFun fun, int httpMethods)，添加普通的http回调接口，其中httpMethods以数据的不同位职位代表不同的http方法，具体如下：
         #define W_HTTP_GET      (1 << 0)
         #define W_HTTP_POST     (1 << 1)
         #define W_HTTP_PUT      (1 << 2)
         #define W_HTTP_DELETE   (1 << 3)
         #define W_HTTP_HEAD     (1 << 4)
         #define W_HTTP_ALL      (1 << 15)
8. void addChunkHttpApi(const string &uri, HttpCbFun fun, int httpMethods)，添加数据块http回调接口，当客户端的http请求数据可能超过3M时，采用这个函数添加接口，典型的场景如文件上传
9. void setHttpFilter(HttpFilterFun filter)，设置http接口的过滤函数，若过滤函数filter返回false，则不会进入回调，用于需要如登录信息才会响应的接口
10. void forceCloseHttpConnection(shared_ptr<HttpReqMsg> httpMsg)，用于强行关闭socket连接，正常情况下，回调完成后，框架内部会自己关闭socket连接。但是有些情况下，比如文件下载时，客户端暂停了下载，而且暂停时间很久，这就导致服务器的某个线程的任务一直被卡主，不得不强制关闭socket
11. void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, const char* data, int len)，向客户端返回数据，会先放入缓冲区，等待下次循环时再发送出去
12. void addSendMsgToQueue(shared_ptr<HttpReqMsg> httpMsg, string *sendMsg)，向客户端返回数据，上面函数的重载
13. void httpReplyJson(shared_ptr<HttpReqMsg> httpMsg, int httpCode, string head, string body)，向客户端返回数据，返回type是json，这是对上面2个函数进一步封装的便捷函数
14. string formJsonBody(int code, string message)，返回类似{"code":0, "message": "success"}的json字符串
15. bool isClientDisconnect(shared_ptr<HttpReqMsg> httpMsg)，返回客户端是否主动断开了连接
16. shared_ptr<string> deQueueHttpChunk(shared_ptr<HttpReqMsg> httpMsg)，大文件上传的场景，获取chunk数据块


#### 重要数据类型

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)

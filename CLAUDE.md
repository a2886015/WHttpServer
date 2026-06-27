# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

WHttpServer 是一个基于 mongoose 7.3（已修改）的 C++ HTTP/HTTPS 服务器框架，使用线程池处理请求，支持 Linux（epoll）、macOS（poll）和 Windows（select）。

## 构建与运行

### 构建命令
```bash
# 在项目根目录执行
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 或直接在根目录构建（CMakeLists.txt 已配置）
cmake .
make -j$(nproc)
```

构建产物输出到 `bin/` 目录，可执行文件名为 `whttp-server`。

### 运行
```bash
./bin/whttp-server
```

服务默认监听 HTTP 6200 端口、HTTPS 6443 端口（见 `HttpExample.cpp`）。

### 系统依赖
- Linux: 需要 OpenSSL 开发库、pthread
- macOS: 需要 `/usr/local/openssl`（CMakeLists.txt 已配置路径）
- 当前 Linux 构建默认开启 AddressSanitizer（`-fsanitize=address,leak,undefined`）

## 代码架构

### 核心库（`whttp-server-core/`）

| 文件 | 职责 |
|------|------|
| `mongoose.cpp/h` | 修改版的 mongoose 7.3 网络库，已移植 epoll/poll 支持，修复了大文件上传和 socket 异步关闭的 bug |
| `WHttpServer.cpp/h` | 主服务器类，封装 mongoose，提供 HTTP/HTTPS 服务、API 注册、线程池调度、静态文件目录、定时器等 |
| `IHttpServer.h` | 服务器接口定义，包含 `HttpReqMsg` 请求消息结构、回调函数类型定义 |
| `WThreadPool.cpp/h` | 动态线程池，支持自动扩缩容（4-256 线程），`concurrentRun()` 返回 `std::future` |
| `LockQueue.hpp` | 线程安全队列（链表实现），用于事件队列、chunk 数据队列、发送队列 |

### 示例代码
- `HttpExample.cpp/h` — 演示 4 种典型场景：普通接口、大文件上传、文件下载、chunk 流下载
- `main.cpp` — 入口，主循环调用 `run(2)` 驱动事件处理

### 关键设计

1. **线程模型**：所有 HTTP 回调在子线程执行，`run()` 在主线程循环调用（非线程安全），`init()`/`addHttpApi()` 等初始化函数必须在 `run()` 之前调用。

2. **两种 API 注册**：
   - `addHttpApi()` — 普通接口，body 一次性接收
   - `addChunkHttpApi()` — 大文件/大数据接口（>3M），数据分块入队，通过 `deQueueHttpChunk()` 逐块取出

3. **URI 匹配**：前缀匹配，不要注册有包含关系的 URI（如 `/test` 和 `/test/dotest`），否则只有先注册的生效。

4. **HTTP 方法**：用位掩码指定，`W_HTTP_GET | W_HTTP_POST` 表示同时支持 GET 和 POST。

5. **长连接**：通过 `setKeepAlive()` 启用，默认连接会处理完请求后关闭。

6. **静态文件目录**：`addStaticWebDir()` 可部署 Web 页面，支持自定义响应头（如 CORS）。

7. **定时器**：`addTimerEvent()` 支持单次/重复执行，返回 timerId 可用于删除。

## 代码规范

- C++11 标准
- 类名：大驼峰（`WHttpServer`）
- 类成员变量：下划线前缀（`_httpPort`）
- 静态成员变量：`s_` 前缀（`s_threadPool`）
- 函数名：小驼峰（`startHttp()`）
- 结构体成员：小驼峰（`httpConnection`）
- 日志宏：`HLogi()`、`HLogw()`、`HLoge()`（带时间戳和线程 ID）

## 重要注意事项

1. **线程安全**：回调函数可能在不同线程执行，需注意共享数据保护。
2. **URI 设计**：避免注册可能互相匹配的 URI，框架不提供 `next()` 机制。
3. **大文件处理**：上传用 `addChunkHttpApi()` + `deQueueHttpChunk()`，下载用 Range 头 + `forceCloseHttpConnection()` 处理客户端中断。
4. **HTTPS 证书**：`cert/` 目录包含自签名证书和生成脚本 `formHttpsCert.sh`。

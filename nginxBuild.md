# 库版本信息  
nginx的版本是1.29.8  
openssl的版本是1.1.1w  
pcre库的版本是8.45，这个库是nginx支持http必须的依赖库，下载地址：https://gitee.com/src-openeuler/pcre  

# 编译步骤  
```
auto/configure --prefix=/usr/local/nginx --with-openssl=/Users/wenke/build/openssl-OpenSSL_1_1_1w --with-pcre=/Users/wenke/build/pcre-8.45  --with-http_ssl_module --with-http_v2_module

make -j8
make insall

```

特别说明：ngnix编译时指定的openssl和pcre的路径不是已经编译好的库，而是需要对应的源码，这点和很多库的编译都不相同  


# nginx常用指令
启动：/usr/local/nginx/sbin/nginx -c /usr/local/nginx/conf/nginx.conf  
快速停止： /usr/local/nginx/sbin/nginx -s stop  
优雅停止（推荐），这个命令会让 Nginx 不再接受新连接，等待当前正在处理的请求全部完成后，再关闭服务。这在生产环境中更安全，可以避免用户请求中断：  
/usr/local/nginx/sbin/nginx -s quit  
重载配置：/usr/local/nginx/sbin/nginx -s reload  


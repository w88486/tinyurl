# tinyurl
短地址服务，实现短地址管理和短地址重定向到源地址
包括以下服务
- 短地址管理
- 重定向
## 短地址管理
使用方法：
1. 编译
编译时确保安装必要的库
```shell
cc -g tinyurl-cli.c -o tinyurl-cli -L/usr/lib/openssl -lssl -lcrypto -lhiredis
```
2. 运行
```shell
./tinyurl-cli ip
```
ip为服务器所在主机地址
界面及提供操作如下：
```txt
请输入选项：
-------------------------------
        1. 生成短地址
        2. 解析短地址
        3. 数据显示
        4. 统计信息
        5. 修改有效期
        6. 修改带宽
        7. 设置阅后即焚
        q. 退出程序
-------------------------------
```
## 重定向
本服务，实现了简单http头部解析，以提供短地址到长地址的重定向功能
使用方法：
1. 编译
```shell
cc -g tinyurl-http.c -o tinyurl-http -L/usr/lib/openssl -lssl -lcrypto -lhiredis
```
2. 跳转
http://youaddress:port/short_url

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <hiredis/hiredis.h>
#include "tool.h"
#include "redistool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#define PORT 9999
#define MAXCON 1000
#define BUFSIZE 1024
#define REDISPORT 6379
typedef struct arg
{
    int fd;
    char ip[16];
} Arg;

__thread char *messege;
__thread int pos;

void *redirect(void *);
int httpstart(int port, int maxcon);
int httpFill(char *key, char *value);
int getContent(Arg arg, char *key, char *value, int len);
void httpErr(Arg arg);
void httpResponse(Arg arg);
void httpResInit();
void httpResFini();
int main(int argc, char const *argv[])
{
    struct sockaddr_in client_addr;
    int fd = httpstart(PORT, MAXCON);
    int len;
    // 接收
    while (1)
    {
        Arg *arg = malloc(sizeof(Arg));
        if (-1 != (arg->fd = accept(fd, (struct sockaddr *)&client_addr, &len)))
        {
            signal(SIGABRT, SIG_IGN);
            // 使用线程做并发
            pthread_t p;
            inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, arg->ip, 16);
            if (pthread_create(&p, NULL, redirect, arg))
            {
                perror("创建任务失败！\n");
                continue;
            }
        }
    }
    // 发送
    // 关闭
    close(fd);
    return 0;
    return 0;
}

int httpstart(int port, int maxcon)
{
    // 创建套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    // 填写地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // 绑定地址
    int len = sizeof(server_addr);
    if (-1 == bind(fd, (struct sockaddr *)&server_addr, len))
    {
        perror("绑定失败\n");
        return -1;
    }
    // 监听
    printf("开始监听端口%d...\n", port);
    listen(fd, maxcon);
    return fd;
}

int getContent(Arg arg, char *key, char *value, int len)
{
    char buf[BUFSIZE];
    int size;
    while (0 != (size = mgets(buf, BUFSIZE, arg.fd)))
    {
        printf("%s", buf);
        char *pos = strstr(buf, key);
        if (NULL != pos && 0 == strncmp(pos, key, strlen(key)))
        {
            pos += strlen(key);
            strncpy(value, pos, len);
            return 1;
        }
    }
    return 0;
}

void *redirect(void *args)
{
    Arg arg = *(Arg *)args;
    char short_url[10];
    if (0 != getContent(arg, "GET ", short_url, 7))
    {
        // 初始化
        redisContext *ctx = redisConnect("127.0.0.1", REDISPORT);
        httpResInit();
        // 获取源地址
        char *pos;
        // 获取https://**.com/ 之后的部分 https://t.cn/0ca4JG
        pos = strrchr(short_url, '/');
        // 0ca4JG run 139.159.195.171
        if (NULL == pos)
        {
            pos = short_url;
        }
        else
        {
            ++pos;
        }
        if ('\0' == *pos || !isalnum(*pos))
        {
            httpErr(arg);
        }
        else
        {
            char *s_url = getOriginUrl(ctx, pos);
            if (NULL == s_url)
            {
                httpErr(arg);
                close(arg.fd);
                free(args);
                httpResFini();
                pthread_exit(NULL);
            }
            // 填充字段
            httpFill("STATS", "307 Temporary Redirect");
            httpFill("Server", "TinyurlServer");
            httpFill("Location", s_url);
            httpFill("Content-Length", "0");
            httpFill("Connection", "close");
            httpFill(NULL, NULL);
            // 发送报文
            printf("发送：\n%s", messege);
            httpResponse(arg);
            free(s_url);
        }
        // 释放资源
        httpResFini();
        redisFree(ctx);
    }
    close(arg.fd);
    free(args);
}

void httpResInit()
{
    messege = malloc(9);
    strncpy(messege, "HTTP/1.1 ", 9);
    pos = 9;
}

int httpFill(char *key, char *value)
{
    // 状态行填充
    if (NULL == key || NULL == value)
    {
        messege = realloc(messege, pos + 3);
        int len = sprintf(messege + pos, "\r\n");
        pos += len;
    }
    else if (0 == strncmp(key, "STATS", 5))
    {
        messege = realloc(messege, pos + strlen(value) + 2);
        int len = sprintf(messege + pos, "%s\r\n", value);
        pos += len;
    }
    else
    // 普通头部字段填充
    {
        messege = realloc(messege, pos + strlen(key) + strlen(value) + 4);
        int len = sprintf(messege + pos, "%s: %s\r\n", key, value);
        pos += len;
    }
}

void httpErr(Arg arg)
{
    messege = realloc(messege, pos + 18);
    int len = sprintf(messege + pos, "404 Not Found\r\n");
    pos += len;
    writemsg(arg.fd, messege, pos);
}

void httpResponse(Arg arg)
{
    writemsg(arg.fd, messege, pos);
}

void httpResFini()
{
    free(messege);
}
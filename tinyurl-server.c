#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <hiredis/hiredis.h>
#define HOST "127.0.0.1"
#define REDIS_PORT 6379
#define PORT 8888
#define USER "test"
#define PW "123456"
#define DB "WSN"

sig_atomic_t stop = 0;

typedef struct messege
{
    enum {
        QUIT = 0,
        GENERATE,
        DECODE,
        MONITOR,
        STATS,
        SETLEFTTIME,
        SETCOUNT,
        SETBURN
    }type;
    /* data */

    union
    {
        // 有效期
        long long date;
        int sum;
    }field1;
     
    union
    {
        // 带宽次数
        long long int count;
        int countvalid;
    }field2;

    union
    {
        // 访问次数
        long long int looked;
        int countdeadline;
    }field3;
    
    // 原url
    char url[2048];
    // 生成的短url
    char short_url[12];
    // 是否阅后即焚
    char isburn;
}messege;

enum Res
{
    DATA = 0,
    INPUTERR,
    OPERAERR,
    SELECTERR,
    DEADLINE,
    COUNT
};
int readmsg(int fd, void *msg, int left_byte)
{
    int pos = 0;
    int size = 0;
    // 重置各条件
    fd_set readfds;
    errno = 0;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    if (0 <= select(fd + 1, &readfds, NULL, &readfds, NULL))
    {
        while(left_byte > 0)
        {
            size = recv(fd, (char*)msg + pos, left_byte, MSG_DONTWAIT);
            if (-1 == size && EAGAIN == errno)
            {
                errno = 0;
                continue;
            }
            else if (0 == size)
            {
                break;
            }
            else if (-1 != size)
            {
                pos += size;
                left_byte -= size;
            }
        }
    }
    return pos;
}

int writemsg(int fd, void *msg, int left_byte)
{
    int pos = 0;
    int size;
    errno = 0;
    // 文件写描述符
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    if (0 > select(fd + 1, NULL, &fds, NULL, NULL))
    {
        return -1;
    }
    while(left_byte > 0)
    {
        size = send(fd, (char*)msg + pos, left_byte, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (EPIPE == errno)
        {
            break;
        }
        if (-1 == size)
        {
            continue;
        }
        pos += size;
        left_byte -= size;
    }
    return pos;
}
void redisplay()
{
    system("clear");
    printf("请输入选项：\n");
    printf("-------------------------------\n"
           "\t1. 生成短地址\n"
           "\t2. 解析短地址\n"
           "\t3. 数据显示\n"
           "\t4. 统计信息\n"
           "\t5. 修改有效期\n"
           "\t6. 修改带宽\n"
           "\t7. 设置阅后即焚\n"
           "\tq. 退出程序\n"
           "-------------------------------\n");
}
// 利用mysql的UUID_SHORT()函数生成一个UUID
long int generate_uuid(redisContext *con)
{
    redisReply *reply = redisCommand(con, "incr uuid");
    if (REDIS_REPLY_ERROR == reply->type)
    {
        fprintf(stderr, "incr uuid error: %s\n", reply->str);
        return -1;
    }
    return reply->integer;
}
// 翻转
void reverse(char *str)
{
    int len = strlen(str);
    for (int i = 0; i < len / 2; ++i)
    {
        char tmp = str[i];
        str[i] = str[len - i - 1];
        str[len - i - 1] = tmp;
    }
}
// 转换成n位的b进制的数的字符
int to_n_base(long int num, int n, int b, char *ret)
{
    char *base = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i = 0;
    while (num > 0 && n > 0)
    {
        ret[i++] = base[num % b];
        num /= b;
        --n;
    }
    while (n > 0)
    {
        ret[i++] = '0';
        --n;
    }
    ret[i] = '\0';
    reverse(ret);
}
// 生成短地址
void generate(redisContext *con, int fd, messege *msg)
{
    // 唯一标识
    long int uuid;
    // 生成的短url
    char short_url[12];
    // 结果
    redisReply *res;
    // 生成唯一标识
    uuid = generate_uuid(con);
    // 转换成62进制字符
    to_n_base(uuid, 6, 62, short_url);
    // 存入数据库 https://blog.csdn.net/USBdrivers/article/details/17028425
    enum Res ret;
    int argc = 10;
    char url[20];
    char count[10];
    int urllen = sprintf(url, "url:%s", short_url);
    int countlen = sprintf(count, "%lld", msg->field2.count);
    char isburn[1];
    isburn[0] = msg->isburn;
    const char *sql[] = {"hmset", url, "looked", "0", "s_url", msg->url, "count", count, "isburn", isburn};
    size_t arglen[] = {5, urllen, 6, 1, 5, strlen(msg->url), 5, countlen, 6, 1};
    res = redisCommandArgv(con, argc, sql, arglen);
    if (NULL == res || con->err || REDIS_REPLY_ERROR == res->type)
    {
        freeReplyObject(res);
        ret = OPERAERR;
        write(fd, &ret, sizeof(ret));
        return;
    }
    freeReplyObject(res);
    // 设置有效期
    if (msg->field1.date > 0)
    {
        argc = 3;
        char date[10];
        int datelen = sprintf(date, "%lld", msg->field1.date);
        const char *sql[3] = {"expire", url, date};
        size_t arglen[3] = {6, urllen, datelen};
        res = redisCommandArgv(con, argc, sql, arglen);
        if (0 == res->integer)
        {
            freeReplyObject(res);
            ret = OPERAERR;
            write(fd, &ret, sizeof(ret));
            return;
        }
        freeReplyObject(res);
    }
    // 加入short_url集合
    res = redisCommand(con, "sadd short_url %s", short_url);
    if (REDIS_REPLY_ERROR == res->type)
    {
        ret = OPERAERR;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    freeReplyObject(res);
    ret = DATA;
    write(fd, &ret, sizeof(ret));
    strncpy(msg->short_url, short_url, sizeof(msg->short_url));
    writemsg(fd, msg, sizeof(messege));
    return;
}
// 解析短地址
void decode(redisContext *con, int fd, messege *msg)
{
    redisReply *res;
    int looked = 0, count = 0;

    // 查询url
    enum Res ret;
    res = redisCommand(con, "hmget url:%s s_url count looked isburn", msg->short_url);
    if (NULL == res || REDIS_REPLY_ERROR == res->type || REDIS_REPLY_NIL == res->element[0]->type)
    {
        ret = SELECTERR;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    // 备份
    looked = strtoll(res->element[2]->str, NULL, 10);
    count = strtoll(res->element[1]->str, NULL, 10);
    strncpy(msg->url, res->element[0]->str, res->element[0]->len + 1);
    // 是否点数用尽
    if (0 == count)
    {
        ret = COUNT;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    // 是否过期
    redisReply *ttl;
    ttl = redisCommand(con, "ttl url:%s", msg->short_url);
    if (REDIS_REPLY_INTEGER == ttl->type && ttl->integer < -1)
    {
        ret = DEADLINE;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(ttl);
        freeReplyObject(res);
        return;
    }
    freeReplyObject(ttl);
    // 阅后即焚
    if ('y' == res->element[3]->str[0])
    {
        redisCommand(con, "del url:%s", msg->short_url);
        redisCommand(con, "srem url:%s", msg->short_url);
        ret = DATA;
        write(fd, &ret, sizeof(ret));
        write(fd, msg, sizeof(messege));
        freeReplyObject(res);
        return;
    }
    freeReplyObject(res);
    // 更改次数
    ++looked;
    --count;
    res = redisCommand(con, "hmset url:%s count %d looked %d", msg->short_url, count, looked);
    if (REDIS_REPLY_ERROR == res->type)
    {
        ret = OPERAERR;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    ret = DATA;
    write(fd, &ret, sizeof(ret));
    writemsg(fd, msg, sizeof(messege));
    freeReplyObject(res);
}
// 数据显示
void monitor(redisContext *con, int fd, messege *msg)
{
    redisReply *res;
    redisReply *short_url;
    // 获取所有短url
    enum Res ret;
    short_url = redisCommand(con, "keys url:*");
    if (NULL == short_url || REDIS_REPLY_ERROR == short_url->type)
    {
        ret = OPERAERR;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    if (REDIS_REPLY_NIL == short_url->type)
    {
        ret = OPERAERR;
        write(fd, &ret, sizeof(ret));
        freeReplyObject(res);
        return;
    }
    // 接下来是数据
    ret = DATA;
    write(fd, &ret, sizeof(ret));
    // 发送行数
    write(fd, &short_url->elements, sizeof(short_url->elements));
    // 发送行
    for (int i = 0; i < short_url->elements; ++i)
    {
        res = redisCommand(con, "hmget %s s_url count isburn looked", short_url->element[i]->str);
        strncpy(msg->short_url, short_url->element[i]->str, short_url->element[i]->len + 1);
        strncpy(msg->url, res->element[0]->str, res->element[0]->len + 1);
        msg->field2.count = atoi(res->element[1]->str);
        msg->isburn = res->element[2]->str[0];
        msg->field3.looked = atoi(res->element[3]->str);
        freeReplyObject(res);
        // 获取剩余有效时间
        res = redisCommand(con, "ttl %s", short_url->element[i]->str);
        msg->field1.date = res->integer;
        size_t size = writemsg(fd, msg, sizeof(messege));
        freeReplyObject(res);
    }
    printf("\n");
    freeReplyObject(short_url);
}
// 统计信息
void stats(redisContext *con, int fd, messege *msg)
{
    int fieldn = 3;
    // 统计值
    redisReply *res, *short_url;
    // 获取sum结果
    enum Res ret;
    short_url = redisCommand(con, "smembers short_url");
    if (NULL == short_url || REDIS_REPLY_ERROR == short_url->type)
    {
        ret = SELECTERR;
        send(fd, &ret, sizeof(ret), MSG_NOSIGNAL);
        freeReplyObject(short_url);
        return;
    }
    if (REDIS_REPLY_NIL == short_url->type)
    {
        msg->field1.sum = 0;
    }
    msg->field1.sum = short_url->elements;
    msg->field2.countvalid = 0;
    msg->field3.countdeadline = 0;
    // 获取countvalid,countdeadline结果
    for (int i = 0; i < msg->field1.sum; ++i)
    {
        res = redisCommand(con, "hget url:%s count", short_url->element[i]->str);
        if (REDIS_REPLY_NIL == res->type)
        {
            ++msg->field3.countdeadline;
        }
        else if (REDIS_REPLY_STRING == res->type && 0 != strcmp(res->str, "0"))
        {
            ++msg->field2.countvalid;
        }
    }
    // 显示
    // 发送结果
    ret = DATA;
    send(fd, &ret, sizeof(ret), MSG_NOSIGNAL);
    writemsg(fd, msg, sizeof(messege));
    freeReplyObject(res);
    freeReplyObject(short_url);
}
// 修改截止日期
void setlefttime(redisContext *con, int fd, messege *msg)
{
    redisReply *res;
    enum Res ret;
    int cmdlen = msg->field1.date == -1 ? 7 : 6;
    int argc = 3;
    char url[20];
    char date[10];
    char *cmd;
    const char *sql[3];
    size_t arglen[3];
    int urllen = sprintf(url, "url:%s", msg->short_url);
    if (msg->field1.date == -1)
    {
        argc = 2;
        cmd =  "PERSIST";
        sql[0] = cmd;
        sql[1] = url;
        arglen[0] = cmdlen;
        arglen[1] = urllen;
    }
    else
    {
        argc = 3;
        cmd = "EXPIRE";
        int datelen = sprintf(date, "%lld", msg->field1.date);
        sql[0] = cmd;
        sql[1] = url;
        sql[2] = date;
        arglen[0] = cmdlen;
        arglen[1] = urllen;
        arglen[2] = datelen;
    }
    res = redisCommandArgv(con, argc, sql, arglen);
    if (NULL == res || REDIS_REPLY_ERROR == res->type)
    {
        ret = OPERAERR;
        freeReplyObject(res);
    }
    if (REDIS_REPLY_INTEGER == res->type)
    {
        ret = DATA;
    }
    else
    {
        ret = OPERAERR;
    }
    write(fd, &ret, sizeof(ret));
    freeReplyObject(res);
}
// 修改点数
void setcount(redisContext *con, int fd, messege *msg)
{
    redisReply *res;
    enum Res ret;
    int argc = 4;
    char count[10];
    int countlen = sprintf(count, "%lld", msg->field2.count);
    char url[20];
    int urllen = sprintf(url, "url:%s", msg->short_url);
    const char *sql[] = {"HSET", url, "count" ,count};
    const size_t arglen[] = {4, urllen, 5, countlen};
    res = redisCommandArgv(con, argc, sql, arglen);
    if (NULL == res || REDIS_REPLY_ERROR == res->type)
    {
        ret = OPERAERR;
    }
    else
    {
        ret = DATA;
    }
    write(fd, &ret, sizeof(ret));
    freeReplyObject(res);
}

// 设置是否阅后即焚
void setburn(redisContext *con, int fd, messege *msg)
{
    redisReply *res;
    enum Res ret;
    int argc = 4;
    char isburn[1];
    isburn[0] = msg->isburn;
    char url[20];
    int urllen = sprintf(url, "url:%s", msg->short_url);
    const char *sql[] = {"HSET", url, "isburn" , isburn};
    const size_t arglen[] = {4, urllen, 6, 1};
    res = redisCommandArgv(con, argc, sql, arglen);
    if (NULL == res || REDIS_REPLY_ERROR == res->type)
    {
        ret = OPERAERR;
    }
    else
    {
        ret = DATA;
    }
    write(fd, &ret, sizeof(ret));
    freeReplyObject(res);
}
void* choice(void*arg)
{
    int fd = *(int*)arg;
    redisContext *con = redisConnect("127.0.0.1", REDIS_PORT);
    if (NULL == con)
    {
        perror("redis连接创建失败！\n");
        pthread_exit(NULL);
    }
    printf("%d连接成功...\n", fd);
    // 主循环
    for (;;)
    {
        int flag = 0;
        // 消息
        messege msg = {0};
        readmsg(fd, &msg, sizeof(msg));
        switch (msg.type)
        {
        case GENERATE:
            generate(con, fd, &msg);
            break;
        case DECODE:
            decode(con, fd, &msg);
            break;
        case MONITOR:
            monitor(con, fd, &msg);
            break;
        case STATS:
            stats(con, fd, &msg);
            break;
        case SETLEFTTIME:
            setlefttime(con, fd, &msg);
            break;
        case SETCOUNT:
            setcount(con, fd, &msg);
            break;
        case SETBURN:
            setburn(con, fd, &msg);
            break;
        case QUIT:
            flag = 1;
            break;
        }
        if (flag)
        {
            break;
        }
    }
    close(fd);
    free(arg);
    redisFree(con);
    printf("连接%d关闭...\n", fd);
}

void handler(int)
{
    stop = 1;
}

int main(int argc, char const *argv[])
{
    signal(SIGINT, handler);
    if (argc < 1)
    {
        perror("使用：tinyurl-server");
        exit(EXIT_FAILURE);
    }
    // 初始化连接
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == fd)
    {
        perror("初始化失败！\n");
        return EXIT_FAILURE;
    }
    // 填充地址
    struct sockaddr_in severaddr;
    struct sockaddr_in clientaddr;
    severaddr.sin_family = AF_INET;
    severaddr.sin_port = htons(PORT);
    severaddr.sin_addr.s_addr = INADDR_ANY;
    if (-1 == bind(fd, (struct sockaddr*)&severaddr, sizeof(severaddr)))
    {
        perror("绑定失败！\n");
        return EXIT_FAILURE;
    }
    // 监听
    listen(fd, 1024);

    while(1)
    {
        if (stop)
        {
            break;
        }
        int *clientfd = malloc(sizeof(int));
        int len;
        if (-1 == (*clientfd = accept(fd, (struct sockaddr*)&clientaddr, &len)))
        {
            perror("连接失败！\n");
            continue;
        }
        pthread_t p;
        if (pthread_create(&p, NULL, choice, clientfd))
        {
            perror("创建任务失败！\n");
            continue;
        }
    }
    printf("服务器关闭！\n");
    close(fd);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <hiredis/hiredis.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#define PORT 27032

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
// 生成短地址
int generate(int con)
{
    system("clear");
    messege msg;
    msg.type = GENERATE;
    // 唯一标识
    long int uuid;
    // 生成的短url
    char short_url[12];
    // 有效期
    long long date;
    // 访问次数
    long long int count;
    // 是否阅后即焚
    int c;
    char *isburn;
    // 结果
    printf("请输入url：");
    fgets(msg.url, 2048, stdin);
    if (msg.url[strlen(msg.url) - 1] == '\n')
    {
        msg.url[strlen(msg.url) - 1] = '\0';
    }
    // 设置有效期
    printf("请输入有效期(秒)：");
    scanf("%lld", &msg.field1.date);
    // 设置访问次数
    printf("请输入带宽：");
    scanf("%llu", &msg.field2.count);
    // 设置是否阅后即焚 run 139.159.195.171
    printf("是否阅后即焚(y/n)：");
    do
    {
        getchar();
        c = getchar();
        if ('y' == c || 'Y' == c)
        {
            msg.isburn = 'y';
            break;
        }
        else if ('n' == c || 'N' == c)
        {
            msg.isburn = 'n';
            break;
        }
        else
        {
            fprintf(stderr, "输入错误！\n");
        }
    } while (1);
    // 存入数据库 https://blog.csdn.net/USBdrivers/article/details/17028425
    writemsg(con, &msg, sizeof(msg));
    // 读取结果 run 139.159.195.171
    enum Res ret;
    read(con, &ret, sizeof(ret));
    readmsg(con, &msg, sizeof(msg));
    if (DATA == ret)
    {
        printf("生成短地址：%s\n", msg.short_url);
    }
    else
    {
        perror("生成失败!\n");
    }
    return 1;
}
// 解析短地址
int decode(int con)
{
    system("clear");
    messege msg;
    char short_url[20], *pos;
    int looked = 0, count = 0;

    printf("请输入tiny url：");
    fgets(short_url, 12, stdin);
    if (short_url[strlen(short_url) - 1] == '\n')
    {
        short_url[strlen(short_url) - 1] = '\0';
    }
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
        fprintf(stderr, "输入的短地址有误！\n");
        return 0;
    }
    strncpy(msg.short_url, pos, strlen(pos) + 1);
    // 发送命令
    msg.type = DECODE;
    writemsg(con, &msg, sizeof(msg));
    enum Res ret;
    read(con, &ret, sizeof(ret));
    switch(ret)
    {
        case SELECTERR:
            fprintf(stderr, "查询失败！");
            break;
        case DEADLINE:
            fprintf(stderr, "该短地址已过期\n");
            break;
        case COUNT:
            fprintf(stderr, "该短地址带宽已用完\n");
            break;
        case OPERAERR:
            fprintf(stderr, "更改失败\n");
            break;
        default:
            readmsg(con, &msg, sizeof(msg));
            printf("%s对应的url为%s\n", msg.short_url, msg.url);
    }
    return 1;
}
// 数据显示
int monitor(int con)
{
    system("clear");
    char *header[] = {"短url", "原url", "剩余带宽", "是否阅后即焚", "浏览次数", "剩余有效时间"};
    size_t rown, fieldn = 4;
    // 发送命令run 139.159.195.171
    messege msg;
    msg.type = MONITOR;
    size_t size = writemsg(con, &msg, sizeof(msg));
    // 根据结果操作
    enum Res ret;
    read(con, &ret, sizeof(ret));
    if (OPERAERR == ret)
    {
        perror("没有数据！\n");
        return 0;
    }
    // 读取行数
    read(con, &rown, sizeof(size_t));
    // 显示
    printf("行数：%ld\n", rown);
    printf("列数：%ld\n", fieldn);
    size = 0;
    for (int i = 0; i < rown; ++i)
    {
        size = readmsg(con, &msg, sizeof(messege));
        printf("----------------------row %d-------------------------------\n", i);
        printf("%20.20s: %s\n", header[0], msg.short_url);
        printf("%20.20s: %s\n", header[1], msg.url);
        printf("%20.20s: %lld\n", header[2], msg.field2.count);
        printf("%20.20s: %c\n", header[3], msg.isburn);
        printf("%20.20s: %lld\n", header[4], msg.field3.looked);
        printf("%20.20s: %lld\n", header[5], msg.field1.date);
    }
    printf("\n");
    return 1;
}
// 统计信息
int stats(int con)
{
    system("clear");
    // 发送命令 run 139.159.195.171
    messege msg;
    msg.type = STATS;
    int left_byte = sizeof(msg);
    int pos = 0;
    int size;
    errno = 0;
    // 文件写描述符
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(con, &fds);
    if (0 > select(con + 1, NULL, &fds, &fds, NULL))
    {
        return 0;
    }
    while(left_byte > 0)
    {
        size = send(con, (char*)&msg + pos, left_byte, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (EPIPE == errno)
        {
            fprintf(stderr, "对端关闭！\n");
            return 0;
        }
        if (-1 == size)
        {
            continue;
        }
        pos += size;
        left_byte -= size;
    }
    errno = 0;
    enum Res ret;
    size = read(con, &ret, sizeof(ret));
    // 出错
    if (SELECTERR == ret)
    {
        perror("查询出错！\n");
        return 0;
    }
    // 有数据，读结果
    // 重置各条件
    left_byte = sizeof(msg);
    pos = 0;
    errno = 0;
    // 检查读条件
    FD_ZERO(&fds);
    FD_SET(con, &fds);
    if (0 <= select(con + 1, &fds, NULL, NULL, NULL))
    {
        while(left_byte > 0)
        {
            size = recv(con, (char*)&msg  + pos, left_byte, MSG_DONTWAIT);
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
    // 显示
    printf("总数\t有效\t到期\t\n");
    printf("%d\t%d\t%d\n", msg.field1.sum, msg.field2.countvalid, msg.field3.countdeadline);
    return 1;
}
// 修改截止日期
int setlefttime(int con)
{
    system("clear");
    messege msg;
    msg.type = SETLEFTTIME;
    printf("请输入要修改的短网址：");
    scanf("%s", msg.short_url);
    printf("请输入有效时间：");
    scanf("%lld", &msg.field1.date);
    int left_byte = sizeof(msg);
    int pos = 0;
    int size;
    errno = 0;
    // 检测文件是否可写
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(con, &fds);
    if (0 > select(con + 1, NULL, &fds, &fds, NULL))
    {
        return 0;
    }
    while(left_byte > 0)
    {
        size = send(con, (char*)&msg + pos, left_byte, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (EPIPE == errno)
        {
            fprintf(stderr, "对端关闭！\n");
            return 0;
        }
        if (-1 == size)
        {
            continue;
        }
        pos += size;
        left_byte -= size;
    }
    // 读取结果
    enum Res ret;
    read(con, &ret, sizeof(ret));
    if (OPERAERR == ret)
    {
        fprintf(stderr, "设置失败!\n");
        return 0;
    }
    else
    {
        printf("修改成功\n");
        return 1;
    }
}
// 修改点数
int setcount(int con)
{
    system("clear");
    messege msg;
    msg.type = SETCOUNT;
    printf("请输入要修改的短网址：");
    scanf("%s", msg.short_url);
    printf("请输入新的带宽：");
    scanf("%lld", &msg.field2.count);
    int left_byte = sizeof(msg);
    int pos = 0;
    int size;
    errno = 0;
    // 检测文件写状态 run 139.159.195.171
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(con, &fds);
    if (0 > select(con + 1, NULL, &fds, &fds, NULL))
    {
        return 0;
    }
    while(left_byte > 0)
    {
        size = send(con, (char*)&msg + pos, left_byte, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (EPIPE == errno)
        {
            fprintf(stderr, "对端关闭！\n");
            return 0;
        }
        if (-1 == size)
        {
            continue;
        }
        pos += size;
        left_byte -= size;
    }
    // 读取结果
    enum Res ret;
    read(con, &ret, sizeof(ret));
    if (OPERAERR == ret)
    {
        fprintf(stderr, "设置失败!\n");
        return 0;
    }
    printf("修改成功\n");
    return 1;
}

// 设置是否阅后即焚
int setburn(int con)
{
    system("clear");
    messege msg;
    printf("请输入要修改的短网址：");
    scanf("%s", msg.short_url);
    printf("请输入是否阅后即焚（y/n）：");
    getchar();
    char c;
    c = getchar();
    if ('y' == c || 'Y' == c)
    {
        msg.isburn = 'y';
    }
    else if ('n' == c || 'N' == c)
    {
        msg.isburn = 'n';
    }
    else
    {
        fprintf(stderr, "输入错误！\n");
        return 0;
    }
    msg.type = SETBURN;
    int left_byte = sizeof(msg);
    int pos = 0;
    int size;
    errno = 0;
    // 文件写描述符 run 139.159.195.171
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(con, &fds);
    if (0 > select(con + 1, NULL, &fds, &fds, NULL))
    {
        return 0;
    }
    while(left_byte > 0)
    {
        size = send(con, (char*)&msg + pos, left_byte, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (EPIPE == errno)
        {
            fprintf(stderr, "对端关闭！\n");
            return 0;
        }
        if (-1 == size)
        {
            continue;
        }
        pos += size;
        left_byte -= size;
    }
    // 读取结果
    enum Res ret = OPERAERR;
    read(con, &ret, sizeof(ret));
    if (OPERAERR == ret)
    {
        fprintf(stderr, "设置失败!\n");
        return 0;
    }
    printf("修改成功\n");
    return 1;
}
int main(int argc, char const *argv[])
{
    if (argc < 2)
    {
        perror("使用：tinyurl-cli ip");
        exit(EXIT_FAILURE);
    }
    // 初始化连接
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == fd)
    {
        fprintf(stderr, "初始化失败！\n");
        return EXIT_FAILURE;
    }
    // 填充地址
    struct sockaddr_in severaddr;
    severaddr.sin_family = AF_INET;
    severaddr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &severaddr.sin_addr.s_addr);
    if (-1 == connect(fd, (struct sockaddr*)&severaddr, sizeof(severaddr)))
    {
        fprintf(stderr, "连接失败！\n");
        return EXIT_FAILURE;
    }
    // 主循环
    int ch;
    for (;;)
    {
        redisplay();
        ch = getchar();
        getchar();
        switch (ch)
        {
        case '1':
            generate(fd);
            break;
        case '2':
            decode(fd);
            break;
        case '3':
            monitor(fd);
            break;
        case '4':
            stats(fd);
            break;
        case '5':
            setlefttime(fd);
            break;
        case '6':
            setcount(fd);
            break;
        case '7':
            setburn(fd);
            break;
        case 'q':
            messege msg;
            msg.type = QUIT;
            write(fd, &msg, sizeof(messege));
            close(fd);
            exit(EXIT_SUCCESS);
        default:
            fprintf(stderr, "输入错误！");
        }
        printf("按'q'键返回...");
        while (getchar() != 'q');
        getchar();
    }
    close(fd);
    return 0;
}
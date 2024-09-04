#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "redistool.h"

char* getOriginUrl(redisContext *con, char *short_url)
{
    redisReply *res;
    int looked = 0, count = 0;
    // 查询url
    res = redisCommand(con, "hmget url:%s s_url count looked isburn", short_url);
    if (NULL == res || REDIS_REPLY_ERROR == res->type || REDIS_REPLY_NIL == res->element[0]->type)
    {
        freeReplyObject(res);
        return NULL;
    }
    // 备份
    looked = strtoll(res->element[2]->str, NULL, 10);
    count = strtoll(res->element[1]->str, NULL, 10);
    char *url = malloc(res->element[0]->len + 1);
    strncpy(url, res->element[0]->str, res->element[0]->len + 1);
    // 是否点数用尽
    if (0 == count)
    {
        freeReplyObject(res);
        return NULL;
    }
    // 是否过期
    redisReply *ttl;
    ttl = redisCommand(con, "ttl url:%s", short_url);
    if (REDIS_REPLY_INTEGER == ttl->type && ttl->integer < -1)
    {
        freeReplyObject(ttl);
        freeReplyObject(res);
        return NULL;
    }
    freeReplyObject(ttl);
    // 阅后即焚
    if ('y' == res->element[3]->str[0])
    {
        redisCommand(con, "del url:%s", short_url);
        redisCommand(con, "srem url:%s", short_url);
        freeReplyObject(res);
        return url;
    }
    freeReplyObject(res);
    // 更改次数
    ++looked;
    --count;
    res = redisCommand(con, "hmset url:%s count %d looked %d", short_url, count, looked);
    if (REDIS_REPLY_ERROR == res->type)
    {

        freeReplyObject(res);
        return NULL;
    }
    freeReplyObject(res);
    return url;
}
//
//  netlib.h
//  anetlib
//
//  Created by jk on 2021/1/21.
//

#ifndef netlib_h
#define netlib_h

#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>

#include "ae.h"
#include "anet.h"


#define REDIS_OK                0
#define REDIS_ERR               -1

#define REDIS_DEFAULT_SYSLOG_IDENT "netlib"
#define REDIS_DEFAULT_TCP_KEEPALIVE 0
#define REDIS_DEFAULT_LOGFILE ""
#define REDIS_DEFAULT_SYSLOG_ENABLED 0
#define REDIS_SERVERPORT        6379    /* TCP port */
#define REDIS_TCP_BACKLOG       511     /* TCP listen backlog */

#define REDIS_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */

/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_VERBOSE 1
#define REDIS_NOTICE 2
#define REDIS_WARNING 3
#define REDIS_LOG_RAW (1<<10) /* Modifier to log without timestamp */
#define REDIS_DEFAULT_VERBOSITY REDIS_NOTICE


#define REDIS_NOTUSED(V) ((void) V)
#define REDIS_IP_STR_LEN INET6_ADDRSTRLEN

#define REDIS_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define REDIS_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
typedef struct redisClient {
    
    
    
    // 套接字描述符
    int fd;
    char querybuf[REDIS_IOBUF_LEN];
    
    char buf[REDIS_REPLY_CHUNK_BYTES];
    int sentlen;
    int bufpos;
} redisClient;



struct redisServer {
    // 事件状态
    aeEventLoop *el;
    int fd;
    // TCP 监听端口
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    int tcpkeepalive;
    
    
    // 网络错误
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
    
    
    
    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* Is syslog enabled? */
    char *syslog_ident;             /* Syslog ident */
    int syslog_facility;            /* Syslog facility */
    
    // 日志可见性
    int verbosity;                  /* Loglevel in redis.conf */
};



#ifdef __GNUC__
void redisLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void redisLog(int level, const char *fmt, ...);
#endif

void redisLogRaw(int level, const char *msg);
void processInputBuffer(redisClient *c);
redisClient *createClient(int fd);
void freeClient(redisClient *c);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask);
extern struct redisServer server;

#endif /* netlib_h */

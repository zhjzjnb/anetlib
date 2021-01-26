
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
#include <stdbool.h>
#include <signal.h>
#include <execinfo.h>

#include "ae.h"
#include "anet.h"
#include "adlist.h"
#include "zmalloc.h"
#include "sds.h"

#define REDIS_OK                0
#define REDIS_ERR               -1


#define REDIS_DEFAULT_HZ        10      /* Time interrupt calls/sec. */

#define REDIS_DEFAULT_SYSLOG_IDENT "anetlib"
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


#define run_with_period(_ms_) if ((_ms_ <= 1000/server.hz) || !(server.cronloops%((_ms_)/(1000/server.hz))))
#define redisAssert(_e) ( (_e)?(void)0:(redisLog(REDIS_WARNING,"file:%s line:%d\n %s",__FILE__,__LINE__,#_e),0))
#define redisPanic(_e) do{ redisLog(REDIS_WARNING,"file:%s line:%d\n %s",__FILE__,__LINE__,#_e);exit(1);}while(0)

typedef struct redisClient redisClient;

typedef void aeNewClientProc(redisClient *c);
typedef bool aeCheckNewClient(int fd);
typedef void aeCloseClient(redisClient *c);
typedef void aeSignalCallback();
typedef struct redisClient {
    // 套接字描述符
    int fd;

    size_t nread;

    //recv and send buff malloc in aeNewClientProc
    // free in aeCloseClient
    char *querybuf;
    char *buf;

    int sentlen;
    int bufpos;
    void *ud;
} redisClient;



struct redisServer {
    // 事件状态
    aeEventLoop *el;
    int fd;
    // TCP 监听端口
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    int tcpkeepalive;
    
    
    // 一个链表，保存了所有客户端状态结构
    list *clients;              /* List of active clients */
    
    // 网络错误
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
    
    
    
    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* Is syslog enabled? */
    char *syslog_ident;             /* Syslog ident */
    int syslog_facility;            /* Syslog facility */
    
    // 日志可见性
    int verbosity;                  /* Loglevel in redis.conf */
    
    int hz;
    int cronloops;
    
    aeNewClientProc *newClientProc;
    aeFileProc *readProc;
    aeFileProc *writeProc;
    aeCheckNewClient *checkNewClient;
    aeCloseClient *closeClient;
    aeSignalCallback *signalCallback;
};






#ifdef __GNUC__
void redisLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void redisLog(int level, const char *fmt, ...);
#endif

void redisLogRaw(int level, const char *msg);

redisClient *createClient(int fd);
void freeClient(redisClient *c);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void setupSignalHandlers(void);


extern struct redisServer server;

#endif /* netlib_h */

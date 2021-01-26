
#include <stdio.h>
#include <stdlib.h>
#include "netlib.h"
#include "ae.h"
#include "anet.h"
#include "zmalloc.h"

#define logD(format,args...) redisLog(REDIS_DEBUG,format , ##args )
#define logV(format,args...) redisLog(REDIS_VERBOSE,format , ##args )
#define logW(format,args...) redisLog(REDIS_WARNING,format , ##args )

void newClientProc(redisClient *c){
    redisLog(REDIS_VERBOSE, "a client accpet ok %d",c->fd);
    c->querybuf = zmalloc(REDIS_IOBUF_LEN);
    c->buf = zmalloc(REDIS_IOBUF_LEN);
    c->querybuf[0] = 0;
    c->buf[0] = 0;
    
}
void closeClient(redisClient *c) {
    redisLog(REDIS_VERBOSE, "a client will close %d",c->fd);
    zfree(c->querybuf);
    zfree(c->buf);
}

bool checkNewClient(int fd){
    return true;
}

void readFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = (redisClient *) privdata;
    int nread, readlen;
    size_t qblen;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    // 读入长度（默认为 16 MB）
    readlen = REDIS_IOBUF_LEN;


    // 读入内容到查询缓存
    nread = read(fd, c->querybuf, REDIS_IOBUF_LEN);

    // 读入出错
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_VERBOSE, "Reading from client: %s", strerror(errno));
            freeClient(c);
            return;
        }
        // 遇到 EOF
    } else if (nread == 0) {
        redisLog(REDIS_VERBOSE, "Client closed connection");
        freeClient(c);
        return;
    }

    if (nread) {
        logV("Reading from client: %s", c->querybuf);
    } else {
        // 在 nread == -1 且 errno == EAGAIN 时运行

        return;
    }
    // 从查询缓存重读取内容，创建参数，并执行命令
    // 函数会执行到缓存中的所有内容都被处理完为止
    
}



void sendToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = privdata;
    int nwritten = 0;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    while (c->bufpos > 0) {
        nwritten = write(fd, c->buf + c->sentlen, c->bufpos - c->sentlen);
        // 出错则跳出
        if (nwritten <= 0) break;

        c->sentlen += nwritten;

        if (c->sentlen == c->bufpos) {
            c->bufpos = 0;
            c->sentlen = 0;
        }
    }

    // 写入出错检查
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            redisLog(REDIS_VERBOSE, "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    }

    if (c->bufpos == 0) {
        c->sentlen = 0;
        aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
    }
}


void signalCallback(){
    
}


void initServerConfig(void){
    server.tcp_backlog = REDIS_TCP_BACKLOG;
    server.port = REDIS_SERVERPORT;
    server.neterr[0] = '\0';
    server.logfile = zstrdup(REDIS_DEFAULT_LOGFILE);
    server.syslog_enabled = REDIS_DEFAULT_SYSLOG_ENABLED;
    server.syslog_ident = zstrdup(REDIS_DEFAULT_SYSLOG_IDENT);
    server.syslog_facility = LOG_LOCAL0;
    server.tcpkeepalive = 1;
    server.cronloops = 0;
    server.hz = REDIS_DEFAULT_HZ;
    server.clients = listCreate();
    server.newClientProc = newClientProc;
    server.readProc = readFromClient;
    server.checkNewClient = checkNewClient;
    server.closeClient = closeClient;
    server.signalCallback = signalCallback;
}


void sendToRemote(aeEventLoop *el, int fd, void *privdata, int mask){
    redisClient *c = privdata;
    int nwritten = 0;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    while (c->bufpos > 0) {
        nwritten = write(fd, c->buf + c->sentlen, c->bufpos - c->sentlen);
        // 出错则跳出
        if (nwritten <= 0) break;

        c->sentlen += nwritten;

        if (c->sentlen == c->bufpos) {
            c->bufpos = 0;
            c->sentlen = 0;
        }
    }

    // 写入出错检查
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            logV("Error writing to remote: %s", strerror(errno));
            exit(1);
        }
    }

    if (c->bufpos == 0) {
        c->sentlen = 0;
        aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
    }
}

void readFromRemote(aeEventLoop *el, int fd, void *privdata, int mask){
    redisClient *c = (redisClient *) privdata;
    int nread;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    
    // 读入内容到查询缓存
    nread = read(fd,c->querybuf,REDIS_IOBUF_LEN);

    // 读入出错
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_VERBOSE, "Reading from remote: %s", strerror(errno));

            goto error;
        }
        // 遇到 EOF
    } else if (nread == 0) {
        redisLog(REDIS_VERBOSE, "remote closed connection");
        goto error;

    }

    if (nread) {
        redisLog(REDIS_VERBOSE, "Reading from remote: %s", c->querybuf);
        
        
        strcpy(c->buf,"pong");
        c->bufpos = 4;
        
        if(aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendToRemote,c) == AE_ERR){
            goto error;
        }
        
    } else {
        // 在 nread == -1 且 errno == EAGAIN 时运行

        return;
    }

    
    return;
error:

    exit(1);

}

void onConnectRemote(aeEventLoop *el, int fd, void *privdata, int mask) {
    
    int sockerr = 0;
    socklen_t errlen = sizeof(sockerr);
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    if (sockerr) {
        aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
        redisLog(REDIS_WARNING,"Error condition connect remote:%s",strerror(sockerr));
        goto error;
    }
    
    aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
    
    redisClient *c = zmalloc(sizeof(*c));
    memset(c,0,sizeof(*c));
    c->fd = fd;
    newClientProc(c);
    
    if (aeCreateFileEvent(server.el,fd,AE_READABLE,readFromRemote,c) ==AE_ERR){
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        goto error;
    }
    return;
    
error:
    close(fd);
    exit(1);
    return;
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    
    run_with_period(3*1000) {
        logV("time:%d  clients:%d",time(0),listLength(server.clients));
        
        listIter *iter = listGetIterator(server.clients,AL_START_HEAD);
        listNode *node;
        while ((node = listNext(iter)) != NULL) {
            redisClient *c = listNodeValue(node);
            
            c->bufpos = sprintf(c->buf,"tick:%d",server.cronloops);
            c->sentlen = 0;
            
            if(aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendToClient,c) == AE_ERR){
                freeClient(c);
            }
        
        }
    
        listReleaseIterator(iter);
    }
    
    server.cronloops++;
    return 1000/server.hz;
}




typedef int(*aeWriteBuff)(redisClient*, char *, int);

int main(int argc, const char * argv[]) {
    
    initServerConfig();
    
    if(argc==2){
        int port = atoi(argv[1]);
        if (port>0) {
            server.port = port;
        }else{
            redisPanic("error port. ./anetlib port");
        }
        
    }
    
    server.el = aeCreateEventLoop(1024);
    
    server.fd = anetTcpServer(server.neterr,server.port,NULL,server.tcp_backlog);
    if(server.fd == ANET_ERR){
        redisLog(REDIS_WARNING,"tcp listen fail:%s\n",server.neterr);
        exit(1);
    }
    anetNonBlock(NULL,server.fd);
    
    if (aeCreateFileEvent(server.el, server.fd, AE_READABLE,acceptTcpHandler,NULL) == AE_ERR){
        redisPanic("Unrecoverable error creating server.ipfd file event.");
    }
    
    
    char err[256]={0};

    int fd = anetTcpNonBlockConnect(err,"127.0.0.1",6379);;
    printf("fd:%d\n",fd);
    
    if (aeCreateFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE,onConnectRemote,NULL) ==AE_ERR){
        close(fd);
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        return REDIS_ERR;
    }
    
    if(aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) {
        redisPanic("Can't create the serverCron time event.");
        exit(1);
    }
    printf("simple redis network model listen on:%d\n",server.port);
    aeMain(server.el);
    // 服务器关闭，停止事件循环
    aeDeleteEventLoop(server.el);
    return 0;
}

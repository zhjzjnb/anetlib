//
//  main.c
//  anetlib
//
//  Created by jk on 2021/1/21.
//

#include <stdio.h>
#include <stdlib.h>
#include "netlib.h"
#include "ae.h"
#include "anet.h"
#include "zmalloc.h"



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
}

//void connectHost(char *hostname, unsigned short port){
//    struct sockaddr_in sock;
//    struct hostent *hoste;
//    int fd;
//    int lr;
//
//    bzero(&sock, sizeof(sock));
//    sock.sin_family = AF_INET;
//    sock.sin_port = htons(port);
//
//
//    sock.sin_addr.s_addr = inet_addr(hostname);
//    if (sock.sin_addr.s_addr == -1) {
//
//     hoste = gethostbyname(hostname);
//     if (hoste == NULL) {
//         print("获取主机名: %s\n", hostname);
//         return -1;
//     }
//
//     memcpy((void *) &sock.sin_addr.s_addr, hoste->h_addr, sizeof(struct in_addr));
//    }
//
//
//    fd = socket(AF_INET, SOCK_STREAM, 0);
//    if (fd == -1) {
//     print("Cannot Create Socket(%s errno:%d)\n", strerror(errno), errno);
//     return -1;
//    }
//
//    lr = connect(fd, (struct sockaddr *) &sock, sizeof(struct sockaddr_in));
//    if (lr != 0) {
//     print("Cannot connect. (%s errno:%d)\n", strerror(errno), errno);
//     return -1;
//    }
//    return fd;
//}


void sendToRemote(aeEventLoop *el, int fd, void *privdata, int mask){
    
}

void readFromRemote(aeEventLoop *el, int fd, void *privdata, int mask){
    
    
    char buff[256]={0};
    int nread = read(fd,buff,sizeof(buff));
    
    printf("buff len:%d is:%s\n",nread,buff);
}


void onConnectRemote(aeEventLoop *el, int fd, void *privdata, int mask) {
    char tmpfile[256], *err;
    int dfd, maxtries = 5;
    int sockerr = 0, psync_result;
    socklen_t errlen = sizeof(sockerr);
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    if (sockerr) {
        aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
        redisLog(REDIS_WARNING,"Error condition on socket for SYNC: %s",strerror(sockerr));
        goto error;
    }
    
    aeDeleteFileEvent(server.el,fd,AE_READABLE|AE_WRITABLE);
    
    
//    if (aeCreateFileEvent(server.el,fd,AE_WRITABLE,sendToRemote,NULL) ==AE_ERR){
//        close(fd);
//        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
//        return;
//    }
    if (aeCreateFileEvent(server.el,fd,AE_READABLE,readFromRemote,NULL) ==AE_ERR){
        close(fd);
        redisLog(REDIS_WARNING,"Can't create readable event for SYNC");
        return;
    }
    
    
    
    int nwritten = write(fd,"self",4);
    
    printf("onconnect nwritten:%d\n",nwritten);
    
    return;
    
error:
    close(fd);
    return;
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    
    run_with_period(3*1000) {
        printf("time:%d  clients:%d\n",time(0),listLength(server.clients));
        
        listIter *iter = listGetIterator(server.clients,AL_START_HEAD);
        listNode *node;
//        listNode *head = listFirst(server.clients);
        while ((node = listNext(iter)) != NULL) {
            redisClient *c = listNodeValue(node);
            printf("cccc:%d\n",c->fd);
        
            c->bufpos = sprintf(c->buf,"tick:%d\n",server.cronloops);
            c->sentlen = 0;
            
            if(aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,sendReplyToClient,c) == AE_ERR){
                freeClient(c);
            }
        
        }
    
        listReleaseIterator(iter);
    }
    
    server.cronloops++;
    return 1000/server.hz;
}


int main(int argc, const char * argv[]) {

    
    initServerConfig();
    
    if(argc==2){
        int port = atoi(argv[1]);
        if (port>0) {
            server.port = port;
        }else{
            redisPanic("error port. ./socket-server port");
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
//    int fd = anetTcpRawConnect("127.0.0.1",10001);
    int fd = anetTcpNonBlockConnect(err,"127.0.0.1",6379);
//    int fd = anetTcpConnect(err,"127.0.0.1",10001);
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

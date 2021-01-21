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

#define redisPanic(_e) do{ redisLog(REDIS_WARNING,"file:%s line:%d\n %s",__FILE__,__LINE__,#_e);exit(1);}while(0)

void initServerConfig(void){
    server.tcp_backlog = REDIS_TCP_BACKLOG;
    server.port = REDIS_SERVERPORT;
    server.neterr[0] = '\0';
    server.logfile = zstrdup(REDIS_DEFAULT_LOGFILE);
    server.syslog_enabled = REDIS_DEFAULT_SYSLOG_ENABLED;
    server.syslog_ident = zstrdup(REDIS_DEFAULT_SYSLOG_IDENT);
    server.syslog_facility = LOG_LOCAL0;
    server.tcpkeepalive = 1;
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
    printf("simple redis network model listen on:%d\n",server.port);
    aeMain(server.el);
    
    // 服务器关闭，停止事件循环
    aeDeleteEventLoop(server.el);
    return 0;
}

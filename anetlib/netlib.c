
#include <fcntl.h>
#include "netlib.h"
#include "zmalloc.h"

struct redisServer server;
/*============================ Utility functions ============================ */

/* Low level logging. To use only for very big messages, otherwise
 * redisLog() is to prefer. */
void redisLogRaw(int level, const char *msg) {
    const int syslogLevelMap[] = {LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING};
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & REDIS_LOG_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.logfile, "a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp, "%s", msg);
    } else {
        int off;
        struct timeval tv;

        gettimeofday(&tv, NULL);
        off = strftime(buf, sizeof(buf), "%F %H:%M:%S.", localtime(&tv.tv_sec));
        snprintf(buf + off, sizeof(buf) - off, "%03d", (int) tv.tv_usec / 1000);
        fprintf(fp, "[%d] %s %c %s\n", (int) getpid(), buf, c[level], msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
    if (server.syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

void redisLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[REDIS_MAX_LOGMSG_LEN];

    if ((level & 0xff) < server.verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    redisLogRaw(level, msg);
}


/*
 * 释放客户端
 */
void freeClient(redisClient *c) {
    if (c->fd <= 0) {
        return;
    }

    aeDeleteFileEvent(server.el, c->fd, AE_READABLE);
    aeDeleteFileEvent(server.el, c->fd, AE_WRITABLE);
    close(c->fd);


    listNode *ln = listSearchKey(server.clients, c);
    redisAssert(ln != NULL);
    listDelNode(server.clients, ln);
    c->fd = 0;
    server.closeClient(c);
    zfree(c);
}



/*
 * 创建一个新客户端
 */
redisClient *createClient(int fd) {

    // 分配空间
    redisClient *c = zmalloc(sizeof(redisClient));
    memset(c, 0, sizeof(*c));

    /* passing -1 as fd it is possible to create a non connected client.
     * This is useful since all the Redis commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    // 当 fd 不为 -1 时，创建带网络连接的客户端
    // 如果 fd 为 -1 ，那么创建无网络连接的伪客户端
    // 因为 Redis 的命令必须在客户端的上下文中使用，所以在执行 Lua 环境中的命令时
    // 需要用到这种伪终端
    if (fd != -1) {
        // 非阻塞
        anetNonBlock(NULL, fd);
        // 禁用 Nagle 算法
        anetEnableTcpNoDelay(NULL, fd);
        // 设置 keep alive
        if (server.tcpkeepalive)
            anetKeepAlive(NULL, fd, server.tcpkeepalive);
        // 绑定读事件到事件 loop （开始接收命令请求）
        if (aeCreateFileEvent(server.el, fd, AE_READABLE, server.readProc, c) == AE_ERR) {
            close(fd);
            zfree(c);
            return NULL;
        }
    }

    // 套接字
    c->fd = fd;

    listAddNodeTail(server.clients, c);

    // 返回客户端
    return c;
}

static void acceptCommonHandler(int fd, int flags) {
    if (!server.checkNewClient(fd)) {
        return;
    }
    redisClient *c;
    if ((c = createClient(fd)) == NULL) {
        redisLog(REDIS_WARNING, "Error registering fd event for the new client: %s (fd=%d)", strerror(errno), fd);
        close(fd); /* May be already closed, just ignore errors */
        return;
    }
    server.newClientProc(c);
}

#define MAX_ACCEPTS_PER_CALL 1000

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL;
    char cip[REDIS_IP_STR_LEN];
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);

    while (max--) {
        // accept 客户端连接
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                redisLog(REDIS_WARNING,
                         "Accepting client connection: %s", server.neterr);
            return;
        }
        redisLog(REDIS_VERBOSE, "Accepted %s:%d", cip, cport);
        // 为客户端创建客户端状态（redisClient）
        acceptCommonHandler(cfd, 0);
    }
}


void logStackContent(void **sp) {
    int i;
    for (i = 15; i >= 0; i--) {
        unsigned long addr = (unsigned long) sp + i;
        unsigned long val = (unsigned long) sp[i];

        if (sizeof(long) == 4)
            redisLog(REDIS_WARNING, "(%08lx) -> %08lx", addr, val);
        else
            redisLog(REDIS_WARNING, "(%016lx) -> %016lx", addr, val);
    }
}


void logRegisters(ucontext_t *uc) {
    redisLog(REDIS_WARNING, "--- REGISTERS");

/* OSX */
#if defined(__APPLE__)
    /* OSX AMD64 */
#if defined(_STRUCT_X86_THREAD_STATE64) && !defined(__i386__)
    redisLog(REDIS_WARNING,
             "\n"
             "RAX:%016lx RBX:%016lx\nRCX:%016lx RDX:%016lx\n"
             "RDI:%016lx RSI:%016lx\nRBP:%016lx RSP:%016lx\n"
             "R8 :%016lx R9 :%016lx\nR10:%016lx R11:%016lx\n"
             "R12:%016lx R13:%016lx\nR14:%016lx R15:%016lx\n"
             "RIP:%016lx EFL:%016lx\nCS :%016lx FS:%016lx  GS:%016lx",
             (unsigned long) uc->uc_mcontext->__ss.__rax,
             (unsigned long) uc->uc_mcontext->__ss.__rbx,
             (unsigned long) uc->uc_mcontext->__ss.__rcx,
             (unsigned long) uc->uc_mcontext->__ss.__rdx,
             (unsigned long) uc->uc_mcontext->__ss.__rdi,
             (unsigned long) uc->uc_mcontext->__ss.__rsi,
             (unsigned long) uc->uc_mcontext->__ss.__rbp,
             (unsigned long) uc->uc_mcontext->__ss.__rsp,
             (unsigned long) uc->uc_mcontext->__ss.__r8,
             (unsigned long) uc->uc_mcontext->__ss.__r9,
             (unsigned long) uc->uc_mcontext->__ss.__r10,
             (unsigned long) uc->uc_mcontext->__ss.__r11,
             (unsigned long) uc->uc_mcontext->__ss.__r12,
             (unsigned long) uc->uc_mcontext->__ss.__r13,
             (unsigned long) uc->uc_mcontext->__ss.__r14,
             (unsigned long) uc->uc_mcontext->__ss.__r15,
             (unsigned long) uc->uc_mcontext->__ss.__rip,
             (unsigned long) uc->uc_mcontext->__ss.__rflags,
             (unsigned long) uc->uc_mcontext->__ss.__cs,
             (unsigned long) uc->uc_mcontext->__ss.__fs,
             (unsigned long) uc->uc_mcontext->__ss.__gs
    );
    logStackContent((void **) uc->uc_mcontext->__ss.__rsp);
#else
    /* OSX x86 */
    redisLog(REDIS_WARNING,
    "\n"
    "EAX:%08lx EBX:%08lx ECX:%08lx EDX:%08lx\n"
    "EDI:%08lx ESI:%08lx EBP:%08lx ESP:%08lx\n"
    "SS:%08lx  EFL:%08lx EIP:%08lx CS :%08lx\n"
    "DS:%08lx  ES:%08lx  FS :%08lx GS :%08lx",
        (unsigned long) uc->uc_mcontext->__ss.__eax,
        (unsigned long) uc->uc_mcontext->__ss.__ebx,
        (unsigned long) uc->uc_mcontext->__ss.__ecx,
        (unsigned long) uc->uc_mcontext->__ss.__edx,
        (unsigned long) uc->uc_mcontext->__ss.__edi,
        (unsigned long) uc->uc_mcontext->__ss.__esi,
        (unsigned long) uc->uc_mcontext->__ss.__ebp,
        (unsigned long) uc->uc_mcontext->__ss.__esp,
        (unsigned long) uc->uc_mcontext->__ss.__ss,
        (unsigned long) uc->uc_mcontext->__ss.__eflags,
        (unsigned long) uc->uc_mcontext->__ss.__eip,
        (unsigned long) uc->uc_mcontext->__ss.__cs,
        (unsigned long) uc->uc_mcontext->__ss.__ds,
        (unsigned long) uc->uc_mcontext->__ss.__es,
        (unsigned long) uc->uc_mcontext->__ss.__fs,
        (unsigned long) uc->uc_mcontext->__ss.__gs
    );
    logStackContent((void**)uc->uc_mcontext->__ss.__esp);
#endif
/* Linux */
#elif defined(__linux__)
    /* Linux x86 */
#if defined(__i386__)
    redisLog(REDIS_WARNING,
    "\n"
    "EAX:%08lx EBX:%08lx ECX:%08lx EDX:%08lx\n"
    "EDI:%08lx ESI:%08lx EBP:%08lx ESP:%08lx\n"
    "SS :%08lx EFL:%08lx EIP:%08lx CS:%08lx\n"
    "DS :%08lx ES :%08lx FS :%08lx GS:%08lx",
        (unsigned long) uc->uc_mcontext.gregs[11],
        (unsigned long) uc->uc_mcontext.gregs[8],
        (unsigned long) uc->uc_mcontext.gregs[10],
        (unsigned long) uc->uc_mcontext.gregs[9],
        (unsigned long) uc->uc_mcontext.gregs[4],
        (unsigned long) uc->uc_mcontext.gregs[5],
        (unsigned long) uc->uc_mcontext.gregs[6],
        (unsigned long) uc->uc_mcontext.gregs[7],
        (unsigned long) uc->uc_mcontext.gregs[18],
        (unsigned long) uc->uc_mcontext.gregs[17],
        (unsigned long) uc->uc_mcontext.gregs[14],
        (unsigned long) uc->uc_mcontext.gregs[15],
        (unsigned long) uc->uc_mcontext.gregs[3],
        (unsigned long) uc->uc_mcontext.gregs[2],
        (unsigned long) uc->uc_mcontext.gregs[1],
        (unsigned long) uc->uc_mcontext.gregs[0]
    );
    logStackContent((void**)uc->uc_mcontext.gregs[7]);
#elif defined(__X86_64__) || defined(__x86_64__)
    /* Linux AMD64 */
    redisLog(REDIS_WARNING,
    "\n"
    "RAX:%016lx RBX:%016lx\nRCX:%016lx RDX:%016lx\n"
    "RDI:%016lx RSI:%016lx\nRBP:%016lx RSP:%016lx\n"
    "R8 :%016lx R9 :%016lx\nR10:%016lx R11:%016lx\n"
    "R12:%016lx R13:%016lx\nR14:%016lx R15:%016lx\n"
    "RIP:%016lx EFL:%016lx\nCSGSFS:%016lx",
        (unsigned long) uc->uc_mcontext.gregs[13],
        (unsigned long) uc->uc_mcontext.gregs[11],
        (unsigned long) uc->uc_mcontext.gregs[14],
        (unsigned long) uc->uc_mcontext.gregs[12],
        (unsigned long) uc->uc_mcontext.gregs[8],
        (unsigned long) uc->uc_mcontext.gregs[9],
        (unsigned long) uc->uc_mcontext.gregs[10],
        (unsigned long) uc->uc_mcontext.gregs[15],
        (unsigned long) uc->uc_mcontext.gregs[0],
        (unsigned long) uc->uc_mcontext.gregs[1],
        (unsigned long) uc->uc_mcontext.gregs[2],
        (unsigned long) uc->uc_mcontext.gregs[3],
        (unsigned long) uc->uc_mcontext.gregs[4],
        (unsigned long) uc->uc_mcontext.gregs[5],
        (unsigned long) uc->uc_mcontext.gregs[6],
        (unsigned long) uc->uc_mcontext.gregs[7],
        (unsigned long) uc->uc_mcontext.gregs[16],
        (unsigned long) uc->uc_mcontext.gregs[17],
        (unsigned long) uc->uc_mcontext.gregs[18]
    );
    logStackContent((void**)uc->uc_mcontext.gregs[15]);
#endif
#else
    redisLog(REDIS_WARNING,
             "  Dumping of registers not supported for this OS/arch");
#endif
}

static void *getMcontextEip(ucontext_t *uc) {

#if defined(__APPLE__)
    /* OSX >= 10.6 */
#if defined(_STRUCT_X86_THREAD_STATE64) && !defined(__i386__)
    return (void *) uc->uc_mcontext->__ss.__rip;
#else
    return (void*) uc->uc_mcontext->__ss.__eip;
#endif
#elif defined(__linux__)
    /* Linux */
#if defined(__i386__)
    return (void*) uc->uc_mcontext.gregs[14]; /* Linux 32 */
#elif defined(__X86_64__) || defined(__x86_64__)
    return (void*) uc->uc_mcontext.gregs[16]; /* Linux 64 */
#elif defined(__ia64__) /* Linux IA64 */
    return (void*) uc->uc_mcontext.sc_ip;
#endif
#else
    return NULL;
#endif
}


/* Logs the stack trace using the backtrace() call. This function is designed
 * to be called from signal handlers safely. */
void logStackTrace(ucontext_t *uc) {
    void *trace[100];
    int trace_size = 0, fd;
    int log_to_stdout = server.logfile[0] == '\0';

    /* Open the log file in append mode. */
    fd = log_to_stdout ?
         STDOUT_FILENO :
         open(server.logfile, O_APPEND | O_CREAT | O_WRONLY, 0644);
    if (fd == -1) return;

    /* Generate the stack trace */
    trace_size = backtrace(trace, 100);

    /* overwrite sigaction with caller's address */
    if (getMcontextEip(uc) != NULL)
        trace[1] = getMcontextEip(uc);

    /* Write symbols to log file */
    backtrace_symbols_fd(trace, trace_size, fd);

    /* Cleanup */
    if (!log_to_stdout) close(fd);
}


void sigshutdown(int sig, siginfo_t *info, void *secret) {


    ucontext_t *uc = (ucontext_t *) secret;

    struct sigaction act;
    REDIS_NOTUSED(info);

    redisLog(REDIS_WARNING,
             "\n\n=== REDIS BUG REPORT START: Cut & paste starting from here ===");
    redisLog(REDIS_WARNING,
             "    Redis crashed by signal: %d", sig);


    /* Log the stack trace */
    redisLog(REDIS_WARNING, "--- STACK TRACE");
    logStackTrace(uc);

    /* Log INFO and CLIENT LIST */
    redisLog(REDIS_WARNING, "--- INFO OUTPUT");


    redisLog(REDIS_WARNING, "--- CLIENT LIST OUTPUT");

    /* Log dump of processor registers */
    logRegisters(uc);


    /* Make sure we exit with the right signal at the end. So for instance
     * the core will be dumped if enabled. */
    sigemptyset (&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
    act.sa_handler = SIG_DFL;
    sigaction(sig, &act, NULL);
    kill(getpid(), sig);

}


static void sigShutdownHandler(int sig) {
    char *msg;

    switch (sig) {
        case SIGINT:
            msg = "Received SIGINT scheduling shutdown...";
            break;
        case SIGTERM:
            msg = "Received SIGTERM scheduling shutdown...";
            break;
        default:
            msg = "Received shutdown signal, scheduling shutdown...";
    };

    /* SIGINT is often delivered via Ctrl+C in an interactive session.
     * If we receive the signal the second time, we interpret this as
     * the user really wanting to quit ASAP without waiting to persist
     * on disk. */
    if (sig == SIGINT) {
        redisLog(REDIS_WARNING, "You insist... exiting now.");

//        exit(1); /* Exit with an error since this was not a clean shutdown. */
    }

    redisLog(REDIS_WARNING, msg);

}

void setupSignalHandlers(void) {
    redisLog(REDIS_DEBUG, "开始获取信号..");
// 设置信号处理函数
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction act;

/* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
 * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigShutdownHandler;
    sigaction(SIGTERM, &act, NULL);


    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = sigshutdown;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
}

#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>     // sockaddr_in
#include <netinet/ip.h>
#include <arpa/inet.h>      // inet_addr
#include <errno.h>
#include <string.h>
#include <stdlib.h>         // atoi
#include <unistd.h>
#include <fcntl.h>          // fcntl

#define MAX_EPOLL_EVENT     1024
// #define BUF_SIZE            1024
#define BUF_SIZE            128

#define NO_EVENT            0
#define ACCEPT_EVENT        1
#define READ_EVENT          2
#define WRITE_EVENT         3

#define LISTEN_FD_NUM       100
#define LISTEN_START_PORT   9900

#define exitif(s, err_str) do { \
    if(s) { \
        printf("%s: %s(code:%d)\n", err_str, strerror(errno), errno); \
    } \
} while(0);

typedef int (*EVENT_CALLBACK)(int fd, int events, void* args);

typedef struct _user_data {
    int             fd;
    uint32_t        events;
    void*           args;

    EVENT_CALLBACK  accept_cb;
    EVENT_CALLBACK  read_cb;
    EVENT_CALLBACK  write_cb;

    char            send_buf[BUF_SIZE];
    char            recv_buf[BUF_SIZE];
    int             send_buf_len;
    int             recv_buf_len;
} user_data;

// 1个用户数据块有1024个用户数据
typedef struct _user_data_block {
    struct _user_data_block*    next;
    user_data*                  user_data_array;
} user_data_block;

typedef struct _reactor {
    // short               listenfd;
    int                 epfd;
    user_data_block*    head;   // 用户数据块链表，head指向第1个数据快
} reactor;


void reactor_set_event(/* reactor* r, */ int fd, int event_type, void* args);
void reactor_del_event(int fd, int event_type, void* args);

int set_fd_nonblock(int fd) {
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

int init_server(short port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    exitif(-1 == fd, "socket");

    int ret = set_fd_nonblock(fd);
    exitif(-1 == ret, "set_fd_nonblock");

    struct sockaddr_in local_addr;
    int addr_len = sizeof(struct sockaddr_in);
    memset(&local_addr, 0, addr_len);
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);
    ret = bind(fd, (struct sockaddr *)&local_addr, addr_len);
    exitif(-1 == ret, "bind");

    ret = listen(fd, 20);
    exitif(-1 == ret, "listen");

    return fd;
}

// 获取第fd / MAX_EPOLL_EVENT个数据块的第fd % MAX_EPOLL_EVENT个user_data
user_data* reactor_user_data(reactor* r, int fd) {
    if (!r || !r->head) {
        return NULL;
    }
    user_data_block* block = r->head;
    user_data_block* prev_block = NULL;
    int index = fd / MAX_EPOLL_EVENT;
    while (index >= 0) {
        if (!block) {
            block = calloc(sizeof(user_data_block), 1);
            block->next = NULL;
            block->user_data_array = calloc(sizeof(user_data), MAX_EPOLL_EVENT);
            if (NULL == block->user_data_array) {
                free(block);
                block = NULL;
                close(r->epfd);
                exitif(1, "NULL == block->user_data_array");
            }
            
            if (prev_block) {
                prev_block->next = block;
            }
        }
        prev_block = block;
        block = block->next;
        --index;
    }

    return &prev_block->user_data_array[fd % MAX_EPOLL_EVENT];
}

// 没有returngcc为什么没有报错？
reactor* reactor_create() {
    reactor* r = calloc(sizeof(reactor), 1);

    r->epfd = epoll_create(1);
    r->head = calloc(sizeof(user_data_block), 1);
    if (NULL == r->head) {
        close(r->epfd);
        exitif(1, "NULL == r->head");
    }
    r->head->next = NULL;
    r->head->user_data_array = calloc(sizeof(user_data), MAX_EPOLL_EVENT);
    if (NULL == r->head->user_data_array) {
        free(r->head);
        r->head = NULL;
        close(r->epfd);
        exitif(1, "NULL == r->head->user_data_array");
    }

    return r;
}

int reactor_accept_cb(int fd, int events, void* args) {
    // printf("reactor_accept_cb\n");

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(struct sockaddr_in));
    socklen_t peer_addr_len = sizeof(peer_addr);
    int clientfd = accept(fd, (struct sockaddr *)&peer_addr, (socklen_t *)&peer_addr_len);
    exitif(-1 == clientfd, "accept");

    int ret = set_fd_nonblock(clientfd);
    exitif(-1 == ret, "set_fd_nonblock");

    printf("new connection from fd: %d, address:%s:%d\n\n", clientfd, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
    
    reactor_set_event(clientfd, READ_EVENT, args);

    return 0;
}

int reactor_read_cb(int fd, int events, void* args) {
    // printf("reactor_read_cb\n");
    
    reactor* r = args;
    user_data* ud = reactor_user_data(r, fd);

    // TODO: socket是非阻塞的，需要循环接收
    char *recv_buf = ud->recv_buf;
    int n = recv(fd, recv_buf, BUF_SIZE, 0);
    if (n < 0)
    {
        printf("recv errno: %s\n", strerror(errno));
        return -1;
    }
    else if (n == 0)
    {
        printf("client closed, fd: %d\n", fd);
        reactor_del_event(fd, 0, args);
        close(fd);
        // free(ud);
    }
    else
    {
        recv_buf[n] = '\0';
        // printf("recv from fd %d, msg: %s", fd, recv_buf);

        char *send_buf = ud->send_buf;
        ud->recv_buf_len = n;
        ud->send_buf_len = n;
        memcpy(send_buf, recv_buf, n);
        reactor_set_event(fd, WRITE_EVENT, args);
    }

    return 0;
}

int reactor_write_cb(int fd, int events, void* args) {
    // printf("reactor_write_cb\n");

    reactor* r = args;
    user_data* ud = reactor_user_data(r, fd);

    int send_len = ud->send_buf_len;
    char* send_buf = ud->send_buf;
    send_buf[send_len] = '\0';
    int nsend = send(fd, send_buf, send_len, 0);
    if (nsend < 0) {
        printf("send errno: %s\n", strerror(errno));
        return -1;
    }
    else if (nsend == 0) {
        printf("send, client closed, fd: %d\n", fd);
        reactor_del_event(fd, 0, args);
        close(fd);
    }
    else {
        // printf("send to fd %d, msg:%s\n", fd, send_buf);
        reactor_set_event(fd, READ_EVENT, args);
    }

    return 0;
}

// 这里用来设置事件，回调函数
void reactor_set_event(int fd, int event_type, void* args) {
    struct epoll_event watch_event = {0};
    reactor* r = args;
    user_data* ud = reactor_user_data(r, fd);

    ud->fd = fd;
    ud->args = args;
    if (ACCEPT_EVENT == event_type) {
        watch_event.events = EPOLLIN;
        ud->accept_cb = reactor_accept_cb;
    }
    else if (READ_EVENT == event_type) {
        watch_event.events = EPOLLIN;
        ud->read_cb = reactor_read_cb;
    }
    else if (WRITE_EVENT == event_type) {
        watch_event.events = EPOLLOUT;
        ud->write_cb = reactor_write_cb;
    }

    watch_event.data.ptr = ud;

    if (NO_EVENT == ud->events) {
#if 0
        static int s_fds[20000];
        static int s_cnt = 0;
        int i;
        int is_exist = 0;
        for (i = 0; i < s_cnt; ++i) {
            if (s_fds[i] == fd) {
                struct sockaddr_in local_addr;
                int32_t addr_len = sizeof(local_addr);
                getsockname(fd, (struct sockaddr*)&local_addr, &addr_len);
                printf("fd %d already exist! local port: %d, event:%d\n", fd, ntohs(local_addr.sin_port), event_type);
                is_exist = 1;
                break;
            }
        }
        if (!is_exist) {
            s_fds[s_cnt++] = fd;

            struct sockaddr_in local_addr;
            int32_t addr_len = sizeof(local_addr);
            getsockname(fd, (struct sockaddr*)&local_addr, &addr_len);
            printf("add fd %d, local port: %d\n", fd, ntohs(local_addr.sin_port));
        }
#endif

        int ret = epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &watch_event);
        exitif(-1 == ret, "EPOLL_CTL_ADD");
    }
    else {
        int ret = epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &watch_event);
        exitif(-1 == ret, "EPOLL_CTL_MOD");
    }
    ud->events = event_type;
}

void reactor_del_event(int fd, int event_type, void* args) {
    struct epoll_event watch_event = {0};
    reactor* r = args;
    user_data* ud = reactor_user_data(r, fd);
    ud->events = NO_EVENT;
    int ret = epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, &watch_event);
    exitif(-1 == ret, "EPOLL_CTL_DEL");
}

void reactor_create_server(short port, void* args) {
    reactor* r = args;
    int listenfd = init_server(port);
    reactor_set_event(listenfd, ACCEPT_EVENT, r);
}

void reactor_loop(void* args) {
    reactor* r = args;
    // 就绪事件列表
    struct epoll_event ready_events[MAX_EPOLL_EVENT] = {0};
    while (1) {
        int nready = epoll_wait(r->epfd, ready_events, MAX_EPOLL_EVENT, 1000);
        if (nready < 0) {
            if (EINTR == errno) {
                continue;
            }
            exitif(-1 == nready, "epoll_wait");
        }
        // 就绪事件对应于events数组的下标[0, nready - 1]
        int i;
        for (i = 0; i < nready; ++i) {
            user_data* ud = (user_data *)ready_events[i].data.ptr;
            int fd = ud->fd;
            
            if (ACCEPT_EVENT == ud->events) {
                ud->accept_cb(fd, 0, r);
            }
            else {
                // 多个cb，用多个if
                if (EPOLLIN & ready_events[i].events) {
                    ud->read_cb(fd, 0, r);
                }
                if (EPOLLOUT & ready_events[i].events) {
                    // TODO: ET模式需要循环发送
                    ud->write_cb(fd, 0, r);
                }
            }
        }
    }
}

void reactor_free(void* args) {
    reactor* r = args;

    user_data_block* cur;
    while (r->head) {
        cur = r->head;
        r->head = r->head->next;

        if (cur) {
            if (cur->user_data_array)
            {
                free(cur->user_data_array);
                cur->user_data_array = NULL;
            }
            free(cur);
            cur = NULL;
        }
    }

    close(r->epfd);
    // close(r->listenfd);
}

int main(int argc, char *argv[]) {
#if 0
    if (argc < 2) {
        printf("usage: %s port\n", argv[0]);
        return -1;
    }
#endif
    
    // 创建reactor
    reactor* r = reactor_create();

    // 创建server
    int i;
    int port = LISTEN_START_PORT;
    int listenfds[LISTEN_FD_NUM];
    for (i = 0; i < LISTEN_FD_NUM; ++i, ++port) {
        // reactor_create_server(port++, r);
        listenfds[i] = init_server(port);
        reactor_set_event(listenfds[i], ACCEPT_EVENT, r);
        printf("fd %d, port %d\n", listenfds[i], port);
    }

    // 事件循环
    reactor_loop(r);

    // 销毁reactor
    reactor_free(r);

    for (int i = 0; i < LISTEN_FD_NUM; ++i) {
        close(listenfds[i]);
    }

    return 0;
}

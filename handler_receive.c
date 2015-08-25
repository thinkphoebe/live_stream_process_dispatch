/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "handler_receive.h"

#include <inttypes.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAXEVENTS 64

typedef struct _handler_receive
{
    handler_t handler;
    int block_size;
    fd_map_t *fd_map_source;
    handler_receive_event_t on_event;

    callback_in_t cb_in;
    callback_out_t cb_out;
    void *param_in;
    void *param_out;

    pthread_t thrd;
    int exit_flag;

    struct epoll_event *events;
    int sfd;
    int efd;
} handler_receive_t;


static int create_and_bind(const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE; /* All interfaces */

    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0)
    {
        printf("getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0)
        {
            //We managed to bind successfully! 
            break;
        }

        close(sfd);
    }

    if (rp == NULL)
    {
        printf("Could not bind\n");
        return -1;
    }

    freeaddrinfo(result);
    return sfd;
}


static int make_socket_non_blocking (int sfd)
{
    int flags, s;

    //得到文件状态标志
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1)
    {
        printf("fnctl F_GETFL got error: %s", strerror(errno));
        return -1;
    }

    //设置文件状态标志
    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1)
    {
        printf("fnctl F_SETFL got error: %s", strerror(errno));
        return -1;
    }

    return 0;
}


static void* thread_proc(void *arg)
{
    handler_receive_t *h_receive = (handler_receive_t *)arg;
    void *block = NULL;
    int32_t read_size;
    void *p_data;
    struct epoll_event event;
    int s;
    int n, i;

    for (; ;)
    {
        n = epoll_wait(h_receive->efd, h_receive->events, MAXEVENTS, -1);
        for (i = 0; i < n; i++)
        {
            if ((h_receive->events[i].events & EPOLLERR) || (h_receive->events[i].events & EPOLLHUP) ||
                    (!(h_receive->events[i].events & EPOLLIN)))
            {
                //An error has occured on this fd, or the socket is not ready for reading (why were we notified then?) 
                printf("receive epoll error\n");
                close (h_receive->events[i].data.fd);
                continue;
            }
            else if (h_receive->sfd == h_receive->events[i].data.fd)
            {
                //We have a notification on the listening socket, which means one or more incoming connections. 
                for (; ;)
                {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof(in_addr);
                    infd = accept(h_receive->sfd, &in_addr, &in_len);
                    if (infd == -1)
                    {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        {
                            //We have processed all incoming connections. 
                            break;
                        }
                        else
                        {
                            printf("accept error:%s\n", strerror(errno));
                            break;
                        }
                    }

                    //将地址转化为主机名或者服务名, flag参数:以数字名返回主机地址和服务地址
                    s = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0)
                    {
                        //printf("Accepted connection on descriptor %d " "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                    //Make the incoming socket non-blocking and add it to the list of fds to monitor. 
                    s = make_socket_non_blocking(infd);
                    if (s == -1)
                    {
                        //abort ();
                        close(infd);
                        continue;
                    }

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(h_receive->efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1)
                    {
                        printf("epoll_ctl error:%s\n", strerror(errno));
                        //abort ();
                        close(infd);
                        continue;
                    }

                    source_info_t *info = (source_info_t *)malloc(sizeof(source_info_t));
                    if (info == NULL)
                    {
                        close(infd);
                        continue;
                    }
                    memset(info, 0, sizeof(*info));
                    info->fd = infd;
                    if (fd_map_add(h_receive->fd_map_source, infd, info) != 0)
                    {
                        free(info);
                        close(infd);
                        continue;
                    }

                    h_receive->on_event(0, infd);
                }
            }
            else
            {
                //We have data on the fd waiting to be read. Read and display it. 
                //We must read whatever data is available completely, 
                //as we are running in edge-triggered mode and won't get a notification again for the same data. 
                int done = 0;
                int size;
                block = NULL;

                for (; ;)
                {
                    if (block == NULL)
                    {
                        block = h_receive->cb_in(h_receive->param_in, 1);
                        p_data = block + 8;
                        read_size = 0;
                    }

                    size = read(h_receive->events[i].data.fd, p_data + read_size, h_receive->block_size - read_size - 8);
                    if (size == -1)
                    {
                        //If errno == EAGAIN, that means we have read all data. So go back to the main loop. 
                        if (errno != EAGAIN)
                        {
                            perror ("read");
                            done = 1;
                        }
                        break;
                    }
                    else if (size == 0)
                    {
                        //End of file. The remote has closed the connection. 
                        done = 1;
                        break;
                    }

                    read_size += size;
                    if (read_size >= h_receive->block_size - 8)
                    {
                        *(int32_t *)block = h_receive->events[i].data.fd;
                        *(int32_t *)(block + 4) = read_size;
                        //memcpy(block, &h_receive->events[i].data.fd, 4);
                        //memcpy(block + 4, &read_size, 4);
                        //printf("aaa %d\n", read_size);
                        h_receive->cb_out(h_receive->param_out, block, 1);
                        block = NULL;
                    }
                }

                if (block != NULL && read_size > 0)
                {
                    *(int32_t *)block = h_receive->events[i].data.fd;
                    *(int32_t *)(block + 4) = read_size;
                    //memcpy(block, &h_receive->events[i].data.fd, 4);
                    //memcpy(block + 4, &read_size, 4);
                    //printf("bbb %d\n", read_size);
                    h_receive->cb_out(h_receive->param_out, block, 1);
                }

                if (done)
                {
                    //TODO: 只close就可以吗??????

                    //printf("Closed connection on descriptor %d\n", h_receive->events[i].data.fd);

                    //Closing the descriptor will make epoll remove it from the set of descriptors which are monitored. 
                    int fd = h_receive->events[i].data.fd;

                    //TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#if 0
                    source_info_t *info = fd_map_get(h_receive->fd_map_source, fd);
                    if (fd_map_remove(h_receive->fd_map_source, fd) != 0)
                    {
                    }
                    h_receive->on_event(1, fd);
                    free(info);
#endif
                    close(fd);
                }
            }
        }
    }

    return NULL;
}


static int receive_start(handler_t *h)
{
    handler_receive_t *h_receive = (handler_receive_t *)h;
    if (h_receive->thrd != 0)
    {
        printf("[%d] already started\n", __LINE__);
        return -1;
    }
    if (pthread_create(&h_receive->thrd, NULL, thread_proc, h) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }
    return 0;
}


static void receive_destroy(handler_t *h)
{
    handler_receive_t *h_receive = (handler_receive_t *)h;
    if (h_receive->thrd != 0)
    {
        h_receive->exit_flag = 1;
        while (h_receive->exit_flag == 1)
            usleep(10000);
    }
    if (h_receive->sfd != 0)
        close(h_receive->sfd);
    if (h_receive->efd != 0)
        close(h_receive->efd);
    if (h_receive->events != NULL)
        free(h_receive->events);
    free(h);
}


static void receive_set_callback_in(handler_t *h, callback_in_t callback, void *param)
{
    handler_receive_t *h_receive = (handler_receive_t *)h;
    h_receive->cb_in = callback;
    h_receive->param_in = param;
}


static void receive_set_callback_out(handler_t *h, callback_out_t callback, void *param)
{
    handler_receive_t *h_receive = (handler_receive_t *)h;
    h_receive->cb_out = callback;
    h_receive->param_out = param;
}


handler_t* handler_receive_create(int block_size, int port, fd_map_t *fd_map_source, handler_receive_event_t on_event)
{
    handler_receive_t *h_receive;
    char port_str[16];
    struct epoll_event event;
    int s;

    h_receive = (handler_receive_t *)malloc(sizeof(handler_receive_t));
    if (h_receive == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_receive, 0, sizeof(*h_receive));
    h_receive->block_size = block_size;
    h_receive->handler.start = receive_start;
    h_receive->handler.destroy = receive_destroy;
    h_receive->handler.set_callback_in = receive_set_callback_in;
    h_receive->handler.set_callback_out = receive_set_callback_out;
    h_receive->fd_map_source = fd_map_source;
    h_receive->on_event = on_event;

    snprintf(port_str, 16, "%d", port);
    h_receive->sfd = create_and_bind(port_str);
    if (h_receive->sfd == -1)
    {
        goto FAIL;
    }

    s = make_socket_non_blocking(h_receive->sfd);
    if (s == -1)
    {
        goto FAIL;
    }

    s = listen(h_receive->sfd, SOMAXCONN);
    if (s == -1)
    {
        printf("listen error:%s\n", strerror(errno));
        goto FAIL;
    }

    //除了参数size被忽略外,此函数和epoll_create完全相同
    h_receive->efd = epoll_create1(0);
    if (h_receive->efd == -1)
    {
        printf("epoll_create1 error:%s\n", strerror(errno));
        goto FAIL;
    }

    event.data.fd = h_receive->sfd;
    event.events = EPOLLIN | EPOLLET;//读入,边缘触发方式
    s = epoll_ctl(h_receive->efd, EPOLL_CTL_ADD, h_receive->sfd, &event);
    if (s == -1)
    {
        printf("epoll_ctl error:%s\n", strerror(errno));
        goto FAIL;
    }

    //Buffer where events are returned 
    h_receive->events = (struct epoll_event *)malloc(MAXEVENTS * sizeof(event));
    if (h_receive->events == NULL)
    {
        goto FAIL;
    }
    //is this needed? memset(h_receive->events, 0, MAXEVENTS * sizeof(event));

    return &h_receive->handler;

FAIL:
    receive_destroy(&h_receive->handler);
    return NULL;
}


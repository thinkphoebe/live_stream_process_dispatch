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
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/epoll.h>

#include "module.h"
#include "module_utils.h"

#define MAXEVENTS 64

typedef struct _module_receive
{
    module_t handler;
    module_info_t info;
    int block_size;

    pthread_t thrd;
    int exit_flag;

    struct epoll_event *events;
    int sfd;
    int efd;
} module_receive_t;


static void* thread_proc(void *arg)
{
    module_receive_t *h_receive = (module_receive_t *)arg;
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

                    modules_data_t *info = (modules_data_t *)malloc(sizeof(modules_data_t));
                    if (info == NULL)
                    {
                        close(infd);
                        continue;
                    }
                    memset(info, 0, sizeof(*info));
                    info->key = infd;
                    if (map_add(h_receive->info.map_data, infd, info) != 0)
                    {
                        free(info);
                        close(infd);
                        continue;
                    }

                    h_receive->info.send_event(h_receive->info.h_event, &h_receive->handler, EVENT_SRC_ADD, (void *)infd);
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
                        block = h_receive->info.cb_in(h_receive->info.param_in, 1);
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
                        h_receive->info.cb_out(h_receive->info.param_out, block, 1);
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
                    h_receive->info.cb_out(h_receive->info.param_out, block, 1);
                }

                if (done)
                {
                    //TODO: 只close就可以吗??????

                    //printf("Closed connection on descriptor %d\n", h_receive->events[i].data.fd);

                    //Closing the descriptor will make epoll remove it from the set of descriptors which are monitored. 
                    int fd = h_receive->events[i].data.fd;

                    //TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#if 0
                    modules_data_t *info = fd_map_get(h_receive->fd_map_source, fd);
                    if (fd_map_remove(h_receive->fd_map_source, fd) != 0)
                    {
                    }
                    h_receive->info.send_event(h_receive->info.h_event, &h_receive->handler, EVENT_SRC_DEL, (void *)fd);
                    free(info);
#endif
                    close(fd);
                }
            }
        }
    }

    return NULL;
}


static int receive_start(module_t *h)
{
    module_receive_t *h_receive = (module_receive_t *)h;
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


static void receive_destroy(module_t *h)
{
    module_receive_t *h_receive = (module_receive_t *)h;
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


static void receive_on_event(module_t *h, int event_id, void *event_data)
{
}


module_t* module_receive_create(const module_info_t *info, int block_size, int port)
{
    module_receive_t *h_receive;
    struct epoll_event event;
    char port_str[16];
    int s;

    h_receive = (module_receive_t *)malloc(sizeof(module_receive_t));
    if (h_receive == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_receive, 0, sizeof(*h_receive));
    h_receive->info = *info;
    h_receive->block_size = block_size;
    h_receive->handler.start = receive_start;
    h_receive->handler.destroy = receive_destroy;
    h_receive->handler.on_event = receive_on_event;

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


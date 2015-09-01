/**
 * @brief accept tcp connect and receive data
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

#define MAXEVENTS 256

typedef struct _module_receive
{
    module_t module;
    module_info_t info;
    int block_size;

    pthread_t thrd;
    int exit_flag;

    struct epoll_event *events;
    int sfd;
    int efd;
} module_receive_t;


static void remove_client(module_receive_t *h_receive, int fd)
{
    struct epoll_event event;
    void *block;
    int ret;

    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    ret = epoll_ctl(h_receive->efd, EPOLL_CTL_DEL, fd, &event);
    if (ret == -1)
        printf("receive EPOLL_CTL_DEL error:%s, fd:%d\n", strerror(errno), fd);

    close(fd);

    block = h_receive->info.cb_in(h_receive->info.param_in, -1);
    BLOCK_FD(block) = 0;
    *(int32_t *)(block + 4) = EVENT_SRC_DEL;
    *(int32_t *)(block + 8) = fd;
    h_receive->info.cb_out(h_receive->info.param_out, block, -1);
}


static void* thread_proc(void *arg)
{
    module_receive_t *h_receive = (module_receive_t *)arg;
    struct epoll_event event;
    int count;
    int ret;
    int i;

    for (; ;)
    {
        if (h_receive->exit_flag != 0)
        {
            printf("receive found exit flag\n");
            h_receive->exit_flag = 0;
            return NULL;
        }

        //发送端进程挂掉时，此处无法立即发现，长时间只是认为对应的fd没有数据
        count = epoll_wait(h_receive->efd, h_receive->events, MAXEVENTS, 100);

        for (i = 0; i < count; i++)
        {
            if ((h_receive->events[i].events & EPOLLERR)
                    || (h_receive->events[i].events & EPOLLHUP)
                    || (!(h_receive->events[i].events & EPOLLIN)))
            {
                printf("receive epoll error\n");
                remove_client(h_receive, h_receive->events[i].data.fd);
                continue;
            }
            else if (h_receive->sfd == h_receive->events[i].data.fd)
            {
                struct sockaddr in_addr;
                socklen_t in_len;
                int in_fd;
                char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
                modules_data_t *info;
                void *block;

                for (; ;)
                {
                    in_len = sizeof(in_addr);
                    in_fd = accept(h_receive->sfd, &in_addr, &in_len);
                    if (in_fd == -1)
                    {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        {
                            //we have processed all incoming connections. 
                            break;
                        }
                        else
                        {
                            printf("receive accept error:%s\n", strerror(errno));
                            break;
                        }
                    }

                    ret = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if (ret == 0)
                    {
                        printf("receive new client, fd:%d, host:%s, port:%s\n", in_fd, hbuf, sbuf);
                    }

                    ret = set_socket_nonblocking(in_fd);
                    if (ret == -1)
                    {
                        printf("receive set socket non-blocking FAILED! fd:%d\n", in_fd);
                        close(in_fd);
                        continue;
                    }

                    memset(&event, 0, sizeof(event));
                    event.data.fd = in_fd;
                    event.events = EPOLLIN | EPOLLET;
                    ret = epoll_ctl(h_receive->efd, EPOLL_CTL_ADD, in_fd, &event);
                    if (ret == -1)
                    {
                        printf("receive EPOLL_CTL_ADD error:%s, fd:%d\n", strerror(errno), in_fd);
                        close(in_fd);
                        continue;
                    }

                    //ATTENTION: 此处malloc的内存，在pipe line的最后一个节点module_process中释放
                    info = (modules_data_t *)malloc(sizeof(modules_data_t));
                    if (info == NULL)
                    {
                        close(in_fd);
                        continue;
                    }
                    memset(info, 0, sizeof(*info));
                    info->key = in_fd;
                    if (map_add(h_receive->info.map_data, in_fd, info) != 0)
                    {
                        free(info);
                        close(in_fd);
                        continue;
                    }

                    block = h_receive->info.cb_in(h_receive->info.param_in, -1);
                    BLOCK_FD(block) = 0;
                    *(int32_t *)(block + 4) = EVENT_SRC_ADD;
                    *(int32_t *)(block + 8) = in_fd;
                    h_receive->info.cb_out(h_receive->info.param_out, block, -1);
                }
            }
            else
            {
                //we must read whatever data is available completely, 
                //as we are running in edge-triggered mode and won't get a notification again for the same data 
                int done = 0;
                void *block = NULL;
                int32_t read_size;
                int size;
                void *p_data;

                for (; ;)
                {
                    if (h_receive->exit_flag != 0)
                    {
                        printf("receive found exit flag\n");
                        h_receive->exit_flag = 0;
                        return NULL;
                    }

                    if (block == NULL)
                    {
                        block = h_receive->info.cb_in(h_receive->info.param_in, 200);
                        if (block == NULL)
                            continue;
                        p_data = BLOCK_DATA(block);
                        read_size = 0;
                    }

                    size = read(h_receive->events[i].data.fd, p_data + read_size, h_receive->block_size - read_size - 8);
                    if (size == -1)
                    {
                        //errno == EAGAIN means that we have read all data
                        if (errno != EAGAIN)
                        {
                            printf("receive read got error:%s\n", strerror(errno));
                            done = 1;
                        }
                        break;
                    }
                    else if (size == 0)
                    {
                        //end of file. the remote has closed the connection 
                        done = 1;
                        break;
                    }

                    read_size += size;
                    if (read_size >= h_receive->block_size - 8)
                    {
                        BLOCK_FD(block) = h_receive->events[i].data.fd;
                        BLOCK_DATA_SIZE(block) = read_size;
                        h_receive->info.cb_out(h_receive->info.param_out, block, -1);
                        block = NULL;
                    }
                }

                if (block != NULL && read_size > 0)
                {
                    BLOCK_FD(block) = h_receive->events[i].data.fd;
                    BLOCK_DATA_SIZE(block) = read_size;
                    h_receive->info.cb_out(h_receive->info.param_out, block, -1);
                }

                if (done)
                    remove_client(h_receive, h_receive->events[i].data.fd);
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
    printf("close thread receive ok\n");

    if (h_receive->sfd != 0)
        close(h_receive->sfd);
    if (h_receive->efd != 0)
        close(h_receive->efd);
    free(h_receive->events);
    free(h);
}


module_t* module_receive_create(const module_info_t *info, int block_size, int port)
{
    module_receive_t *h_receive;
    struct epoll_event event;
    int ret;

    h_receive = (module_receive_t *)malloc(sizeof(module_receive_t));
    if (h_receive == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_receive, 0, sizeof(*h_receive));
    h_receive->info = *info;
    h_receive->block_size = block_size;
    h_receive->module.start = receive_start;
    h_receive->module.destroy = receive_destroy;

    h_receive->sfd = serve_socket(port);
    if (h_receive->sfd == -1)
        goto FAIL;

    ret = set_socket_nonblocking(h_receive->sfd);
    if (ret == -1)
        goto FAIL;

    ret = listen(h_receive->sfd, SOMAXCONN);
    if (ret == -1)
    {
        printf("listen error:%s\n", strerror(errno));
        goto FAIL;
    }

    //除了参数size被忽略外, 此函数和epoll_create完全相同
    h_receive->efd = epoll_create1(0);
    if (h_receive->efd == -1)
    {
        printf("epoll_create1 error:%s\n", strerror(errno));
        goto FAIL;
    }

    memset(&event, 0, sizeof(event));
    event.data.fd = h_receive->sfd;
    event.events = EPOLLIN | EPOLLET; //读入, 边缘触发
    ret = epoll_ctl(h_receive->efd, EPOLL_CTL_ADD, h_receive->sfd, &event);
    if (ret == -1)
    {
        printf("epoll_ctl error:%s\n", strerror(errno));
        goto FAIL;
    }

    h_receive->events = (struct epoll_event *)malloc(MAXEVENTS * sizeof(event));
    if (h_receive->events == NULL)
    {
        printf("malloc FAILED! %d\n", (int)(MAXEVENTS * sizeof(event)));
        goto FAIL;
    }

    return &h_receive->module;

FAIL:
    receive_destroy(&h_receive->module);
    return NULL;
}


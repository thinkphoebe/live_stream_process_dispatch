/**
 * @brief foward received data to udp port or tcp connections
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <sys/epoll.h>

#include "module.h"
#include "module_utils.h"

#define MAXEVENTS 64
#define MAX_FD 65536
#define MAXCLIENTS 4096
#define PKG_MAX 8192

//#define UDP

typedef struct _module_resend
{
    module_t module;
    module_info_t info;

    map_t *map_out;

    pthread_t thrd;
    int exit_flag;

    int out_count;

    struct epoll_event *events;
    int sfd;
    int efd;

    int fdin_1;
    int fdin_2;
} module_resend_t;

typedef struct _client_t 
{
#ifdef UDP
    struct sockaddr_in send2;
#endif
    void *block_addr;
    int block_index;
    int block_size;
    int block_pos;

    modules_data_t *p_srcinfo;
    int fd;
} client_t;

typedef struct _info_resend
{
    //这里简单起见，使用数组，后续应改成链表
    client_t clients[MAXCLIENTS];
    int client_count;

    //简单起见使用数组，应改成链表，否则设置小了可能导致所有client被阻塞
    void *pkgs[PKG_MAX];
    int pkg_head;
    int pkg_tail;
} info_resend_t;


static void remove_client(module_resend_t *h_resend, int fd)
{
    struct epoll_event event;
    modules_data_t *info;
    info_resend_t *info_resend;
    client_t *client;
    int ret;
    int i;

    event.data.fd = fd;
    ret = epoll_ctl(h_resend->efd, EPOLL_CTL_DEL, fd, &event);
    if (ret == -1)
        printf("resend EPOLL_CTL_DEL error:%s, fd:%d\n", strerror(errno), fd);

    close(fd);

    client = map_get(h_resend->map_out, fd);
    if (client == NULL)
        return;
    info = client->p_srcinfo;
    info_resend = get_module_data(info, h_resend->info.module_id);

    for (i = 0; i < info_resend->client_count; i++)
    {
        if (&info_resend->clients[i] == client)
        {
            info_resend->clients[i] = info_resend->clients[info_resend->client_count - 1];
            break;
        }
    }
    info_resend->client_count--;
    if (info_resend->client_count == 0)
    {
        for (i = info_resend->pkg_head; i != info_resend->pkg_tail; i = (i + 1) % PKG_MAX)
            h_resend->info.cb_out(h_resend->info.param_out, info_resend->pkgs[i], -1);
    }

    map_remove(h_resend->map_out, fd);
}


static void process_event(module_resend_t *h_resend, void *block)
{
    modules_data_t *info;
    info_resend_t *info_resend;
    int32_t event_id = *(int32_t *)(block + 4);
    int32_t fd = *(int32_t *)(block + 8);

    info = map_get(h_resend->info.map_data, fd);

    if (event_id == EVENT_SRC_ADD)
    {
        info_resend = (info_resend_t *)malloc(sizeof(info_resend_t));
        if (info_resend == NULL)
        {
        }
        memset(info_resend, 0, sizeof(sizeof(info_resend_t)));
        set_module_data(info, MODULE_RESEND, info_resend);

#ifdef UDP
        if (info_resend->client_count > MAXCLIENTS)
            return;

        client_t *client;
        int i;
        for (i = 0; i < 2; i++) //添加两个用做测试
        {
            uint32_t dest_ip;
            uint16_t dest_port;

            //测试方便起见，转发到固定端口.
            dest_port = htons(10000 + i * 10000 + fd);

            //这里将少数流转发到外部机器，用来检验正确性，其他发到本机
            if (h_resend->out_count < 2)
            {
                dest_ip = inet_addr("172.16.15.89");
                printf("=================================> stream send to 172.16.15.89 port:%d\n", 10000 + i * 10000 + fd);
                h_resend->out_count += 1;
            }
            else
                dest_ip = inet_addr("127.0.0.1");

            client = &info_resend->clients[i];
            client->send2.sin_family = AF_INET;
            client->send2.sin_addr.s_addr = dest_ip;
            client->send2.sin_port = dest_port;
            client->p_srcinfo = info;

            client->block_addr = NULL;
            client->block_pos = 0;
            client->block_size = 0;
            client->block_index = -1;

            client->fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (client->fd < 0)
            {
            }
            info_resend->client_count += 1;


            struct epoll_event event;
            event.data.fd = client->fd;
            //event.events = EPOLLOUT | EPOLLET;
            event.events = EPOLLOUT;
            int ret = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, client->fd, &event);
            if (ret == -1)
            {
                //TODO
            }

            if (map_add(h_resend->map_out, client->fd, client) != 0)
            {
                //TODO
            }
        }
#else
        if (h_resend->fdin_1 == 0)
            h_resend->fdin_1 = fd;
        else if (h_resend->fdin_2 == 0)
            h_resend->fdin_2 = fd;
#endif
    }
    else if (event_id == EVENT_SRC_DEL)
    {
        printf("source %d deleted, remove its clients\n", fd);
        info_resend = get_module_data(info, h_resend->info.module_id);
        for (; info_resend->client_count > 0;)
            remove_client(h_resend, info_resend->clients[0].fd);
        free(info_resend);
    }
}


static void move_blocks(module_resend_t *h_resend)
{
    void *block = NULL;
    modules_data_t *info;
    info_resend_t *info_resend;
    int32_t fd;

    for (; ;)
    {
        block = h_resend->info.cb_in(h_resend->info.param_in, 0);
        if (block == NULL)
            break;

        fd = BLOCK_FD(block);
        if (fd == 0)
        {
            process_event(h_resend, block);
            h_resend->info.cb_out(h_resend->info.param_out, block, -1);
            continue;
        }

        info = map_get(h_resend->info.map_data, fd);
        info_resend = get_module_data(info, h_resend->info.module_id);

        if (info_resend->client_count <= 0)
        {
            h_resend->info.cb_out(h_resend->info.param_out, block, -1);
            continue;
        }

        if ((info_resend->pkg_tail + 1) % PKG_MAX == info_resend->pkg_head)
            break;
        info_resend->pkgs[info_resend->pkg_tail] = block;
        info_resend->pkg_tail = (info_resend->pkg_tail + 1) % PKG_MAX;
    }
}


static void* thread_proc(void *arg)
{
    module_resend_t *h_resend = (module_resend_t *)arg;
    int write_size;
    modules_data_t *info;
    info_resend_t *info_resend;
    int count, i, k;
    struct epoll_event event;
    int ret;
    int write_count;

    for (; ;)
    {
        if (h_resend->exit_flag != 0)
        {
            printf("resend found exit flag\n");
            h_resend->exit_flag = 0;
            return NULL;
        }

        count = epoll_wait(h_resend->efd, h_resend->events, MAXEVENTS, 100);
        write_count = 0;

        for (i = 0; i < count; i++)
        {
            if ((h_resend->events[i].events & EPOLLERR) || (h_resend->events[i].events & EPOLLHUP))
            {
                printf("resend epoll error\n");
                remove_client(h_resend, h_resend->events[i].data.fd);
                continue;
            }
            else if (h_resend->sfd == h_resend->events[i].data.fd)
            {
                struct sockaddr in_addr;
                socklen_t in_len;
                int in_fd;
                char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                for (; ;)
                {
                    in_len = sizeof(in_addr);
                    in_fd = accept(h_resend->sfd, &in_addr, &in_len);
                    if (in_fd == -1)
                    {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        {
                            //we have processed all incoming connections. 
                            break;
                        }
                        else
                        {
                            printf("accept error:%s\n", strerror(errno));
                            break;
                        }
                    }

                    ret = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if (ret == 0)
                    {
                        printf("resend new client, fd:%d, host:%s, port:%s\n", in_fd, hbuf, sbuf);
                    }

                    ret = set_socket_nonblocking(in_fd);
                    if (ret == -1)
                    {
                        close(in_fd);
                        continue;
                    }

                    event.data.fd = in_fd;
                    event.events = EPOLLOUT; //ATTENTION: not add EPOLLET here
                    ret = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, in_fd, &event);
                    if (ret == -1)
                    {
                        printf("epoll_ctl error:%s\n", strerror(errno));
                        close(in_fd);
                        continue;
                    }

                    //TODO: 根据请求的信息，将这个client分配到某个source. 
                    //这里测试简单起见, 记录下第一、第二个source, 随机分配到这两个
                    modules_data_t *info;
                    if (atoi(sbuf) % 2 == 0)
                        info = map_get(h_resend->info.map_data, h_resend->fdin_1);
                    else
                        info = map_get(h_resend->info.map_data, h_resend->fdin_2);
                    info_resend = get_module_data(info, h_resend->info.module_id);


                    if (info_resend->client_count > MAXCLIENTS)
                    {
                        close(in_fd);
                        continue;
                    }
                    client_t *client;
                    client = &info_resend->clients[info_resend->client_count];
                    info_resend->client_count += 1;
                    client->block_addr = NULL;
                    client->block_pos = 0;
                    client->block_size = 0;
                    client->block_index = -1;
                    client->p_srcinfo = info;
                    client->fd = in_fd;

                    if (map_add(h_resend->map_out, in_fd, client) != 0)
                    {
                       free(info);
                       close(in_fd);
                       continue;
                    }
                }
            }
            else
            {
                move_blocks(h_resend);

                client_t *client = map_get(h_resend->map_out, h_resend->events[i].data.fd);
                if (client == NULL)
                    continue;
                info = client->p_srcinfo;
                info_resend = get_module_data(info, h_resend->info.module_id);

                if (client->block_index < 0)
                {
                    if (info_resend->pkg_head != info_resend->pkg_tail)
                    {
                        client->block_index = info_resend->pkg_head;
                        client->block_addr = info_resend->pkgs[info_resend->pkg_head] + 8;
                        client->block_size = *(int32_t *)(info_resend->pkgs[info_resend->pkg_head] + 4);
                        client->block_pos = 0;
                    }
                    else
                    {
                        //没有数据可以发送
                        continue;
                    }
                }
                else if (client->block_pos >= client->block_size)
                {
                    if ((client->block_index + 1) % PKG_MAX != info_resend->pkg_tail)
                    {
                        //检查此block是否是head，并且没有其他client在使用，是的话向下移动
                        if (client->block_index == info_resend->pkg_head)
                        {
                            int used = 0;
                            for (k = 0; k < info_resend->client_count; k++)
                            {
                                client_t *c = &info_resend->clients[k];
                                if (c == client)
                                    continue;
                                if (c->block_index == info_resend->pkg_head)
                                {
                                    used = 1;
                                    break;
                                }
                            }
                            if (used == 0)
                            {
                                info_resend->pkg_head = (info_resend->pkg_head + 1) % PKG_MAX;
                                h_resend->info.cb_out(h_resend->info.param_out, info_resend->pkgs[info_resend->pkg_head], -1);
                            }
                        }

                        client->block_index = (client->block_index + 1) % PKG_MAX;
                        client->block_addr = info_resend->pkgs[client->block_index] + 8;
                        client->block_size = *(int32_t *)(info_resend->pkgs[client->block_index] + 4);
                        client->block_pos = 0;
                    }
                    else
                    {
                        //没有数据可以发送
                        continue;
                    }
                }

#ifdef UDP
                write_size = sendto(h_resend->events[i].data.fd, client->block_addr + client->block_pos,
                        client->block_size - client->block_pos, 0,
                        (struct sockaddr *)&client->send2, sizeof(struct sockaddr_in));
#else
                write_size = send(h_resend->events[i].data.fd, client->block_addr + client->block_pos,
                        client->block_size - client->block_pos, 0);
#endif
                if (write_size == -1)
                {
                    //errno == EAGAIN means that we have read all data
                    if (errno != EAGAIN)
                    {
                        printf("resend send got error:%s\n", strerror(errno));
                        remove_client(h_resend, h_resend->events[i].data.fd);
                    }
                    continue;
                }
                else if (write_size == 0)
                {
                    //end of file. the remote has closed the connection 
                    remove_client(h_resend, h_resend->events[i].data.fd);
                    break;
                }

                write_count += 1;
                if (write_size <= 0)
                    continue;

                client->block_pos += write_size;
            }
        }

        if (write_count == 0)
            usleep(100000);

        if (count <= 0)
            move_blocks(h_resend);
    }

    return NULL;
}


static int resend_start(module_t *h)
{
    module_resend_t *h_resend = (module_resend_t *)h;
    if (h_resend->thrd != 0)
    {
        printf("[%d] already started\n", __LINE__);
        return -1;
    }
    if (pthread_create(&h_resend->thrd, NULL, thread_proc, h) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }
    return 0;
}


static void resend_destroy(module_t *h)
{
    module_resend_t *h_resend = (module_resend_t *)h;
    void *block;

    if (h_resend->thrd != 0)
    {
        h_resend->exit_flag = 1;
        while (h_resend->exit_flag == 1)
            usleep(10000);
    }
    printf("close thread resend ok\n");

    if (h_resend->efd != 0)
        close(h_resend->efd);
    if (h_resend->sfd != 0)
        close(h_resend->sfd);
    if (h_resend->map_out != NULL)
        map_destroy(h_resend->map_out);
    free(h_resend->events);

    //未处理的block回归memory_pool, 用于内存泄漏检查
    for (; ;)
    {
        block = h_resend->info.cb_in(h_resend->info.param_in, 0);
        if (block == NULL)
            break;
        h_resend->info.cb_out(h_resend->info.param_out, block, -1);
    }

    free(h);
}


module_t* module_resend_create(const module_info_t *info, int port)
{
    module_resend_t *h_resend;
    struct epoll_event event;
    int ret;

    h_resend = (module_resend_t *)malloc(sizeof(module_resend_t));
    if (h_resend == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_resend, 0, sizeof(*h_resend));

    h_resend->info = *info;
    h_resend->module.start = resend_start;
    h_resend->module.destroy = resend_destroy;

    //除了参数size被忽略外, 此函数和epoll_create完全相同
    h_resend->efd = epoll_create1(0);
    if (h_resend->efd == -1)
    {
        printf("epoll_create1 error:%s\n", strerror(errno));
        goto FAIL;
    }

    h_resend->sfd = serve_socket(port);
    if (h_resend->sfd == -1)
        goto FAIL;

    ret = set_socket_nonblocking(h_resend->sfd);
    if (ret == -1)
        goto FAIL;

    ret = listen(h_resend->sfd, SOMAXCONN);
    if (ret == -1)
    {
        printf("listen error:%s\n", strerror(errno));
        goto FAIL;
    }

    event.data.fd = h_resend->sfd;
    event.events = EPOLLIN | EPOLLET; //读入, 边缘触发
    ret = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, h_resend->sfd, &event);
    if (ret == -1)
    {
        printf("epoll_ctl error:%s\n", strerror(errno));
        goto FAIL;
    }

    h_resend->events = (struct epoll_event *)malloc(MAXEVENTS * sizeof(struct epoll_event));
    if (h_resend->events == NULL)
    {
        printf("malloc FAILED! %d\n", (int)(MAXEVENTS * sizeof(event)));
        goto FAIL;
    }

    h_resend->map_out = map_create(MAX_FD);
    if (h_resend->map_out == NULL)
        goto FAIL;

    return &h_resend->module;

FAIL:
    resend_destroy(&h_resend->module);
    return NULL;
}


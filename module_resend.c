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
#include <sys/socket.h>
#include <netdb.h>

#include "module.h"
#include "module_utils.h"

#include <inttypes.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXEVENTS 64
#define MAX_FD 65536
#define MAXCLIENTS 4096
#define PKG_MAX 8192

//#define UDP

typedef struct _module_resend
{
    module_t handler;
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
    int fdsend2;
#endif
    void *block_addr;
    int block_index;
    int block_size;
    int block_pos;

    modules_data_t *p_srcinfo;
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


static void* thread_proc(void *arg)
{
    module_resend_t *h_resend = (module_resend_t *)arg;
    void *block = NULL;
    int write_size;
    modules_data_t *info;
    info_resend_t *info_resend;
    int n, i, k;
    struct epoll_event event;
    int s;
    int32_t socket_fd;
    int32_t data_size;
    int write_count;

    for (; ;)
    {
        n = epoll_wait(h_resend->efd, h_resend->events, MAXEVENTS, -1);
        write_count = 0;

        for (i = 0; i < n; i++)
        {
            if ((h_resend->events[i].events & EPOLLERR) || (h_resend->events[i].events & EPOLLHUP))
            {
                //An error has occured on this fd, or the socket is not ready for reading (why were we notified then?) 
                printf("resend epoll error\n");
                close (h_resend->events[i].data.fd);
                map_remove(h_resend->map_out, h_resend->events[i].data.fd);
                continue;
            }
            else if (h_resend->sfd == h_resend->events[i].data.fd)
            {
                //We have a notification on the listening socket, which means one or more incoming connections. 
                for (; ;)
                {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof(in_addr);
                    infd = accept(h_resend->sfd, &in_addr, &in_len);
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
                        printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA Accepted connection on descriptor %d " "(host=%s, port=%s)\n", infd, hbuf, sbuf);
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
                    //event.events = EPOLLIN | EPOLLET;
                    event.events = EPOLLOUT;
                    s = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1)
                    {
                        printf("epoll_ctl error:%s\n", strerror(errno));
                        //abort ();
                        close(infd);
                        continue;
                    }

                    //TODO: 根据请求的信息，将这个client分配到某个source. 
                    //这里测试简单起见, 记录下第一、第二个source, 随机分配到这两个
                    modules_data_t *info;
                    if (atoi(sbuf) % 2 == 0)
                        info = map_get(h_resend->info.map_data, h_resend->fdin_1);
                    else
                        info = map_get(h_resend->info.map_data, h_resend->fdin_2);
                    info_resend = get_module_data(info, MODULE_RESEND);


                    if (info_resend->client_count > MAXCLIENTS)
                    {
                        close(infd);
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

                    if (map_add(h_resend->map_out, infd, client) != 0)
                    {
                       free(info);
                       close(infd);
                       continue;
                    }
                }
            }
            else
            {
                for (; ;)
                {
                    block = h_resend->info.cb_in(h_resend->info.param_in, 0);
                    if (block == NULL)
                    {
                        //printf("no in block\n");
                        break;
                    }
                    //else
                    //{
                    //    printf("get block\n");
                    //}
                    socket_fd = *(int32_t *)block;
                    data_size = *(int32_t *)(block + 4);
                    info = map_get(h_resend->info.map_data, socket_fd);
                    info_resend = get_module_data(info, MODULE_RESEND);

                    if (info_resend->client_count <= 0)
                    {
                        h_resend->info.cb_out(h_resend->info.param_out, block, 1);
                        continue;
                    }

                    if ((info_resend->pkg_tail + 1) % PKG_MAX == info_resend->pkg_head)
                    {
                        //printf("aaaaaaaaaaaaaaaaaaaaaaa %d, %d\n", info_resend->pkg_head, info_resend->pkg_tail);
                        break;
                    }
                    info_resend->pkgs[info_resend->pkg_tail] = block;
                    info_resend->pkg_tail = (info_resend->pkg_tail + 1) % PKG_MAX;
                    //printf("bbbbbb %d, %d, %d\n", info_resend->pkg_head, info_resend->pkg_tail, data_size);
                }

                client_t *client = map_get(h_resend->map_out, h_resend->events[i].data.fd);
                info = client->p_srcinfo;
                info_resend = get_module_data(info, MODULE_RESEND);

                if (client->block_index < 0)
                {
                    if (info_resend->pkg_head != info_resend->pkg_tail)
                    {
                        client->block_index = info_resend->pkg_head;
                        client->block_addr = info_resend->pkgs[info_resend->pkg_head] + 8;
                        client->block_size = *(int32_t *)(info_resend->pkgs[info_resend->pkg_head] + 4);
                        client->block_pos = 0;
                        //printf("UUUUUUUUUUUUUUUUUUUU %d\n", client->block_size);
                    }
                    else
                    {
                        //printf("AAAAAAAAAAAAAAAAAAAA\n");
                        //usleep(100000);
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
                                h_resend->info.cb_out(h_resend->info.param_out, info_resend->pkgs[info_resend->pkg_head], 1);
                                //printf("cccccc %d, %d\n", info_resend->pkg_head, info_resend->pkg_tail);
                            }
                        }

                        client->block_index = (client->block_index + 1) % PKG_MAX;
                        //printf("UUUUU %d, %d, %d, %d\n", client->fdsend2, client->block_index, info_resend->pkg_head, info_resend->pkg_tail);
                        client->block_addr = info_resend->pkgs[client->block_index] + 8;
                        client->block_size = *(int32_t *)(info_resend->pkgs[client->block_index] + 4);
                        client->block_pos = 0;
                    }
                    else
                    {
#ifdef UDP
                        //printf("BBBBB %d, %d, %d, %d\n", client->fdsend2, client->block_index, info_resend->pkg_head, info_resend->pkg_tail);
#endif
                        //usleep(10000);
                        //没有数据可以发送
                        continue;
                    }
                }

#ifdef UDP
                write_size = sendto(client->fdsend2, client->block_addr + client->block_pos,
                        client->block_size - client->block_pos, 0,
                        (struct sockaddr *)&client->send2, sizeof(struct sockaddr_in));
                //printf("CCCCCCCCCCCCCCCCCCCC %p, %d, %d, %d\n", client, client->block_size, client->block_pos, write_size);
#else
                write_size = send(h_resend->events[i].data.fd, client->block_addr + client->block_pos,
                        client->block_size - client->block_pos, 0);
                //printf("DDDDDDDDDDDDDDDDDDDD %p, %d, %d, %d\n", client, client->block_size, client->block_pos, write_size);
#endif

                write_count += 1;
                if (write_size <= 0)
                {
                    continue;
                }

                client->block_pos += write_size;
            }
        }

        if (write_count == 0)
        {
            //printf("no data write uuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuu\n");
            usleep(100000);
        }
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
    if (h_resend->thrd != 0)
    {
        h_resend->exit_flag = 1;
        while (h_resend->exit_flag == 1)
            usleep(10000);
    }
    free(h_resend->events);
    close(h_resend->efd);
    map_destroy(h_resend->map_out);
    free(h);
}


static void resend_on_event(module_t *h, int event_id, void *event_data)
{
    module_resend_t *h_resend = (module_resend_t *)h;
    modules_data_t *info;
    info_resend_t *info_resend;

    info = map_get(h_resend->info.map_data, (int)event_data);

    if (event_id == EVENT_SRC_ADD)
    {
        info_resend = get_module_data(info, MODULE_RESEND);
        if (info_resend == NULL)
        {
            info_resend = (info_resend_t *)malloc(sizeof(info_resend_t));
            if (info_resend == NULL)
            {
            }
            memset(info_resend, 0, sizeof(sizeof(info_resend_t)));
            set_module_data(info, MODULE_RESEND, info_resend);
        }

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

            client->fdsend2 = socket(AF_INET, SOCK_DGRAM, 0);
            if (client->fdsend2 < 0)
            {
            }
            info_resend->client_count += 1;


            struct epoll_event event;
            event.data.fd = client->fdsend2;
            //event.events = EPOLLOUT | EPOLLET;
            event.events = EPOLLOUT;
            int s = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, client->fdsend2, &event);
            if (s == -1)
            {
                //printf("epoll_ctl error:%s\n", strerror(errno));
                ////abort ();
                //close(client->fdsend2);
                //continue;
            }

            if (map_add(h_resend->map_out, client->fdsend2, client) != 0)
            {
                //free(info);
                //close(infd);
                //continue;
            }
        }
#else
        if (h_resend->fdin_1 == 0)
            h_resend->fdin_1 = (int)event_data;
        else if (h_resend->fdin_2 == 0)
            h_resend->fdin_2 = (int)event_data;
#endif
    }
    else if (event_id == EVENT_SRC_DEL)
    {
        //TODO !!!!!!
    }
}


module_t* module_resend_create(const module_info_t *info)
{
    module_resend_t *h_resend;
    char port_str[16];
    struct epoll_event event;
    int s;

    h_resend = (module_resend_t *)malloc(sizeof(module_resend_t));
    if (h_resend == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_resend, 0, sizeof(*h_resend));

    h_resend->info = *info;
    h_resend->handler.start = resend_start;
    h_resend->handler.destroy = resend_destroy;
    h_resend->handler.on_event = resend_on_event;

    //除了参数size被忽略外,此函数和epoll_create完全相同
    h_resend->efd = epoll_create1(0);
    if (h_resend->efd == -1)
    {
        printf("epoll_create1 error:%s\n", strerror(errno));
        goto FAIL;
    }


    snprintf(port_str, 16, "%d", 7000); //TODO !!!!!!
    h_resend->sfd = create_and_bind(port_str);
    if (h_resend->sfd == -1)
    {
        goto FAIL;
    }

    s = make_socket_non_blocking(h_resend->sfd);
    if (s == -1)
    {
        goto FAIL;
    }

    s = listen(h_resend->sfd, SOMAXCONN);
    if (s == -1)
    {
        printf("listen error:%s\n", strerror(errno));
        goto FAIL;
    }

    //除了参数size被忽略外,此函数和epoll_create完全相同
    h_resend->efd = epoll_create1(0);
    if (h_resend->efd == -1)
    {
        printf("epoll_create1 error:%s\n", strerror(errno));
        goto FAIL;
    }

    event.data.fd = h_resend->sfd;
    event.events = EPOLLIN | EPOLLET;//读入,边缘触发方式
    s = epoll_ctl(h_resend->efd, EPOLL_CTL_ADD, h_resend->sfd, &event);
    if (s == -1)
    {
        printf("epoll_ctl error:%s\n", strerror(errno));
        goto FAIL;
    }


    //Buffer where events are returned 
    h_resend->events = (struct epoll_event *)malloc(MAXEVENTS * sizeof(struct epoll_event));
    if (h_resend->events == NULL)
    {
        goto FAIL;
    }

    h_resend->map_out = map_create(MAX_FD);
    if (h_resend->map_out == NULL)
    {
        goto FAIL;
    }

    return &h_resend->handler;

FAIL:
    resend_destroy(&h_resend->handler);
    return NULL;
}


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
#include "handler_resend.h"

#include <inttypes.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXEVENTS 64
#define MAXCLIENTS 32

typedef struct _handler_resend
{
    handler_t handler;
    int block_size;
    fd_map_t *fd_map_source;

    callback_in_t cb_in;
    callback_out_t cb_out;
    void *param_in;
    void *param_out;

    pthread_t thrd;
    int exit_flag;

    int out_count;
} handler_resend_t;

typedef struct _client_t 
{
    struct sockaddr_in send2;
    int fdsend2;
    void *block_addr;
    int block_pos;
} client_t;

typedef struct _info_resend
{
    //这里简单起见，使用数组，后续应改成链表
    client_t clients[MAXCLIENTS];
    int client_count;
} info_resend_t;


static void* thread_proc(void *arg)
{
    handler_resend_t *h_resend = (handler_resend_t *)arg;
    void *block = NULL;
    int write_size;
    source_info_t *info;
    void *p_data;
    int size;
    int32_t socket_fd;
    int32_t data_size;
    int i;

    for (; ;)
    {
        block = h_resend->cb_in(h_resend->param_in, 1);
        socket_fd = *(int32_t *)block;
        data_size = *(int32_t *)(block + 4);
        //memcpy(&socket_fd, block, 4);
        //memcpy(&data_size, block + 4, 4);
        p_data = block + 8;
        info = fd_map_get(h_resend->fd_map_source, socket_fd);

        for (i = 0; i < ((info_resend_t *)info->info_resend)->client_count; i++)
        {
            client_t *client = &((info_resend_t *)info->info_resend)->clients[i];
            write_size = 0;
            for (; ;)
            {
                size = sendto(client->fdsend2, p_data + write_size, data_size, 0,
                        (struct sockaddr *)&client->send2, sizeof(struct sockaddr_in));
                if (size < 0)
                {
                    break;
                }

                write_size += size;
                if (write_size >= data_size)
                {
                    break;
                }
            }
        }

        h_resend->cb_out(h_resend->param_out, block, 1);
    }

    return NULL;
}


static int resend_start(handler_t *h)
{
    handler_resend_t *h_resend = (handler_resend_t *)h;
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


static void resend_destroy(handler_t *h)
{
    handler_resend_t *h_resend = (handler_resend_t *)h;
    if (h_resend->thrd != 0)
    {
        h_resend->exit_flag = 1;
        while (h_resend->exit_flag == 1)
            usleep(10000);
    }
    free(h);
}


static void resend_set_callback_in(handler_t *h, callback_in_t callback, void *param)
{
    handler_resend_t *h_resend = (handler_resend_t *)h;
    h_resend->cb_in = callback;
    h_resend->param_in = param;
}


static void resend_set_callback_out(handler_t *h, callback_out_t callback, void *param)
{
    handler_resend_t *h_resend = (handler_resend_t *)h;
    h_resend->cb_out = callback;
    h_resend->param_out = param;
}


handler_t* handler_resend_create(int block_size, fd_map_t *fd_map_source)
{
    handler_resend_t *h_resend;

    h_resend = (handler_resend_t *)malloc(sizeof(handler_resend_t));
    if (h_resend == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_resend, 0, sizeof(*h_resend));

    h_resend->block_size = block_size;
    h_resend->fd_map_source = fd_map_source;
    h_resend->handler.start = resend_start;
    h_resend->handler.destroy = resend_destroy;
    h_resend->handler.set_callback_in = resend_set_callback_in;
    h_resend->handler.set_callback_out = resend_set_callback_out;

    return &h_resend->handler;

//FAIL:
//    resend_destroy(&h_resend->handler);
//    return NULL;
}


void handler_resend_on_event(handler_t *h, int type, int fd)
{
    handler_resend_t *h_resend = (handler_resend_t *)h;
    source_info_t *info;

    info = fd_map_get(h_resend->fd_map_source, fd);

    if (type == 0)
    {
        info->info_resend = (info_resend_t *)malloc(sizeof(info_resend_t));
        if (info->info_resend == NULL)
        {
        }
        memset(info->info_resend, 0, sizeof(sizeof(info_resend_t)));

        if (((info_resend_t *)info->info_resend)->client_count > MAXCLIENTS)
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

            client = &((info_resend_t *)info->info_resend)->clients[i];
            client->send2.sin_family = AF_INET;
            client->send2.sin_addr.s_addr = dest_ip;
            client->send2.sin_port = dest_port;

            client->fdsend2 = socket(AF_INET, SOCK_DGRAM, 0);
            if (client->fdsend2 < 0)
            {
            }
            ((info_resend_t *)info->info_resend)->client_count += 1;
        }
    }
    else if (type == 1)
    {
        //TODO !!!!!!
    }
}


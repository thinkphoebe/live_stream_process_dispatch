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
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include "module_utils.h"


int set_module_data(modules_data_t *d, int module_id, void *data)
{
    if (module_id < 0 || module_id >= MODULES_MAX)
        return -1;
    d->modules[module_id] = data;
    return 0;
}


void* get_module_data(modules_data_t *d, int module_id)
{
    if (module_id < 0 || module_id >= MODULES_MAX)
        return NULL;
    return d->modules[module_id];
}


void* queue_get_block(queue_t *queue, int wait)
{
    uint8_t *queue_buf;
    int queue_buf_size;
    void *buf;
    for (; ;)
    {
        if (queue_get_readbuf(queue, &queue_buf, &queue_buf_size) != 0)
        {
            if (wait == 0)
                return NULL;
            //printf("read block to from queue FAILED!\n");
            usleep(20000);
            continue;
        }

        memcpy(&buf, queue_buf, sizeof(void *));
        queue_read_complete(queue);
        break;
    }
    return buf;
}


int queue_put_block(queue_t *queue, void *block, int wait)
{
    uint8_t *queue_buf;
    int queue_buf_size = sizeof(void *);
    for (; ;)
    {
        if (queue_get_writebuf(queue, &queue_buf, &queue_buf_size) != 0)
        {
            if (wait == 0)
                return -1;
            //printf("write block to queue FAILED!\n");
            usleep(20000);
            continue;
        }

        memcpy(queue_buf, &block, sizeof(void *));
        queue_write_complete(queue, queue_buf, sizeof(void *), 0);
        return 0;
    }
}


void* memory_pool_get_block(memory_pool_t *memory_pool, int wait)
{
    void *block;
    for (; ;)
    {
        block = memory_pool_alloc(memory_pool);
        if (block == NULL)
        {
            if (wait == 0)
                return NULL;
            //printf("allocate from memory pool FAILED!\n");
            usleep(20000);
            continue;
        }
        return block;
    }
}


int memory_pool_put_block(memory_pool_t *memory_pool, void *block, int wait)
{
    memory_pool_free(memory_pool, block);
    return 0;
}


int create_and_bind(const char *port)
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


int make_socket_non_blocking (int sfd)
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

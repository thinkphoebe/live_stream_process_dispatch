/**
 * @brief some utility functions
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


void* queue_get_block(queue_t *queue, int timeout)
{
    uint8_t *queue_buf;
    int queue_buf_size;
    void *buf;
    for (; ;)
    {
        if (queue_get_readbuf(queue, &queue_buf, &queue_buf_size) != 0)
        {
            //printf("read block to from queue FAILED!\n");
            if (timeout == -1 || timeout >= 20)
                usleep(20000);
            else
                return NULL;
            if (timeout >= 20)
                timeout -= 20;
            continue;
        }

        memcpy(&buf, queue_buf, sizeof(void *));
        queue_read_complete(queue);
        break;
    }
    return buf;
}


int queue_put_block(queue_t *queue, void *block, int timeout)
{
    uint8_t *queue_buf;
    int queue_buf_size = sizeof(void *);
    for (; ;)
    {
        if (queue_get_writebuf(queue, &queue_buf, &queue_buf_size) != 0)
        {
            //printf("write block to queue FAILED!\n");
            if (timeout == -1 || timeout >= 20)
                usleep(20000);
            else
                return -1;
            if (timeout >= 20)
                timeout -= 20;
            continue;
        }

        memcpy(queue_buf, &block, sizeof(void *));
        queue_write_complete(queue, queue_buf, sizeof(void *), 0);
        return 0;
    }
}


void* memory_pool_get_block(memory_pool_t *memory_pool, int timeout)
{
    void *block;
    for (; ;)
    {
        block = memory_pool_alloc(memory_pool);
        if (block == NULL)
        {
            //printf("allocate from memory pool FAILED!\n");
            if (timeout == -1 || timeout >= 20)
                usleep(20000);
            else
                return NULL;
            if (timeout >= 20)
                timeout -= 20;
            continue;
        }
        return block;
    }
}


int memory_pool_put_block(memory_pool_t *memory_pool, void *block, int timeout)
{
    memory_pool_free(memory_pool, block);
    return 0;
}


int serve_socket(int port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    char port_str[16];
    int reuse = 1;
    int fd;
    int ret;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; //return IPv4 and IPv6 choices 
    hints.ai_socktype = SOCK_STREAM; //we want a TCP socket 
    hints.ai_flags = AI_PASSIVE; //all interfaces 

    snprintf(port_str, 16, "%d", port);
    ret = getaddrinfo(NULL, port_str, &hints, &result);
    if (ret != 0)
    {
        printf("getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1)
            continue;

        ret = bind(fd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0)
        {
            //we managed to bind successfully! 
            break;
        }

        close(fd);
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (rp == NULL)
    {
        printf("Could not bind\n");
        return -1;
    }

    freeaddrinfo(result);
    return fd;
}


int set_socket_nonblocking(int fd)
{
    int flags;
    int ret;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        printf("fnctl F_GETFL got error: %s", strerror(errno));
        return -1;
    }

    flags |= O_NONBLOCK;
    ret = fcntl(fd, F_SETFL, flags);
    if (ret == -1)
    {
        printf("fnctl F_SETFL got error: %s", strerror(errno));
        return -1;
    }

    return 0;
}

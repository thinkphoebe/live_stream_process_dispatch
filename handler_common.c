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
#include "handler_common.h"


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



struct _fd_map
{
    int max_fd;
    void **fds;
};


fd_map_t* fd_map_create(int max_fd)
{
    fd_map_t *h = (fd_map_t *)malloc(sizeof(fd_map_t));
    if (h == NULL)
    {
        return NULL;
    }
    memset(h, 0, sizeof(*h));

    h->max_fd = max_fd;
    h->fds = malloc(sizeof(void *) * (max_fd + 1));
    if (h->fds == NULL)
    {
        free(h);
        return NULL;
    }
    memset(h->fds, 0, sizeof(void *) * (max_fd + 1));
    return h;
}


void fd_map_destroy(fd_map_t *h)
{
    free(h->fds);
    free(h);
}


int fd_map_add(fd_map_t *h, int fd, void *info)
{
    if (fd < 0 || fd > h->max_fd)
    {
        printf("[add] fd [%d] exceed max_fd [%d]\n", fd, h->max_fd);
        return -1;
    }
    h->fds[fd] = info;
    return 0;
}


int fd_map_remove(fd_map_t *h, int fd)
{
    if (fd < 0 || fd > h->max_fd)
    {
        printf("[remove] fd [%d] exceed max_fd [%d]\n", fd, h->max_fd);
        return -1;
    }
    h->fds[fd] = NULL;
    return 0;
}


void* fd_map_get(fd_map_t *h, int fd)
{
    if (fd < 0 || fd > h->max_fd)
    {
        printf("[get] fd [%d] exceed max_fd [%d]\n", fd, h->max_fd);
        return NULL;
    }
    return h->fds[fd];
}


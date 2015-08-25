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
#include "handler_process.h"

typedef struct _handler_process
{
    handler_t handler;
    int block_size;

    callback_in_t cb_in;
    callback_out_t cb_out;
    void *param_in;
    void *param_out;

    pthread_t thrd;
    int exit_flag;
} handler_process_t;


static void* thread_proc(void *arg)
{
    handler_process_t *h = (handler_process_t *)arg;
    void *block;
    int32_t socket_fd;
    int32_t data_size;
    void *p_data;
    for (; ;)
    {
        block = h->cb_in(h->param_in, 1);
        socket_fd = *(int32_t *)block;
        data_size = *(int32_t *)(block + 4);
        //memcpy(&socket_fd, block, 4);
        //memcpy(&data_size, block + 4, 4);
        p_data = block + 8;

        //TODO: do some process

        h->cb_out(h->param_out, block, 1);
    }
    return NULL;
}


static int process_start(handler_t *h)
{
    handler_process_t *h_process = (handler_process_t *)h;
    if (h_process->thrd != 0)
    {
        printf("[%d] already started\n", __LINE__);
        return -1;
    }
    if (pthread_create(&h_process->thrd, NULL, thread_proc, h) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }
    return 0;
}


static void process_destroy(handler_t *h)
{
    handler_process_t *h_process = (handler_process_t *)h;
    if (h_process->thrd != 0)
    {
        h_process->exit_flag = 1;
        while (h_process->exit_flag == 1)
            usleep(10000);
    }
    free(h);
}


static void process_set_callback_in(handler_t *h, callback_in_t callback, void *param)
{
    handler_process_t *h_process = (handler_process_t *)h;
    h_process->cb_in = callback;
    h_process->param_in = param;
}


static void process_set_callback_out(handler_t *h, callback_out_t callback, void *param)
{
    handler_process_t *h_process = (handler_process_t *)h;
    h_process->cb_out = callback;
    h_process->param_out = param;
}


handler_t* handler_process_create(int block_size)
{
    handler_process_t *h_process = (handler_process_t *)malloc(sizeof(handler_process_t));
    if (h_process == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_process, 0, sizeof(*h_process));
    h_process->block_size = block_size;
    h_process->handler.start = process_start;
    h_process->handler.destroy = process_destroy;
    h_process->handler.set_callback_in = process_set_callback_in;
    h_process->handler.set_callback_out = process_set_callback_out;
    return &h_process->handler;
}


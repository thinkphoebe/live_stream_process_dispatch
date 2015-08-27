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
#include "module.h"

typedef struct _module_process
{
    module_t handler;
    module_info_t info;

    pthread_t thrd;
    int exit_flag;
} module_process_t;


static void* thread_proc(void *arg)
{
    module_process_t *h = (module_process_t *)arg;
    void *block;
    int32_t socket_fd;
    int32_t data_size;
    void *p_data;
    for (; ;)
    {
        block = h->info.cb_in(h->info.param_in, 1);
        socket_fd = *(int32_t *)block;
        data_size = *(int32_t *)(block + 4);
        p_data = block + 8;

        //TODO: do some process

        h->info.cb_out(h->info.param_out, block, 1);
    }
    return NULL;
}


static int process_start(module_t *h)
{
    module_process_t *h_process = (module_process_t *)h;
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


static void process_destroy(module_t *h)
{
    module_process_t *h_process = (module_process_t *)h;
    if (h_process->thrd != 0)
    {
        h_process->exit_flag = 1;
        while (h_process->exit_flag == 1)
            usleep(10000);
    }
    free(h);
}


static void process_on_event(module_t *h, int event_id, void *event_data)
{
}


module_t* module_process_create(const module_info_t *info)
{
    module_process_t *h_process = (module_process_t *)malloc(sizeof(module_process_t));
    if (h_process == NULL)
    {
        printf("malloc FAILED!\n");
        return NULL;
    }
    memset(h_process, 0, sizeof(*h_process));
    h_process->info = *info;
    h_process->handler.start = process_start;
    h_process->handler.destroy = process_destroy;
    h_process->handler.on_event = process_on_event;
    return &h_process->handler;
}


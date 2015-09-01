/**
 * @brief a dummy module for demo purpose, you could dump streams here
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
    module_t module;
    module_info_t info;
    pthread_t thrd;
    int exit_flag;
} module_process_t;


static void* thread_proc(void *arg)
{
    module_process_t *h_process = (module_process_t *)arg;
    modules_data_t *data;
    void *block;
    int32_t fd;
    //int32_t data_size;
    //void *p_data;

    for (; ;)
    {
        if (h_process->exit_flag != 0)
        {
            printf("process found exit flag\n");
            h_process->exit_flag = 0;
            return NULL;
        }

        block = h_process->info.cb_in(h_process->info.param_in, 200);
        if (block == NULL)
            continue;
        fd = BLOCK_FD(block);

        if (fd == 0)
        {
            int32_t event = *(int32_t *)(block + 4);
            if (event == EVENT_SRC_DEL)
            {
                int fd = *(int32_t *)(block + 8);
                data = map_get(h_process->info.map_data, fd);
                if (map_remove(h_process->info.map_data, fd) != 0)
                {
                    printf("receive remove source from map FAILED!\n");
                }
                free(data);
                printf("source %d deleted, free module data\n", fd);
            }
        }
        else
        {
            //TODO: do something here
            //data_size = BLOCK_DATA_SIZE(block);
            //p_data = BLOCK_DATA(block);
        }

        h_process->info.cb_out(h_process->info.param_out, block, -1);
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
    void *block;
    int key;
    void *value;

    if (h_process->thrd != 0)
    {
        h_process->exit_flag = 1;
        while (h_process->exit_flag == 1)
            usleep(10000);
    }
    printf("close thread process ok\n");

    //未处理的block回归memory_pool, 用于内存泄漏检查
    for (; ;)
    {
        block = h_process->info.cb_in(h_process->info.param_in, 0);
        if (block == NULL)
            break;
        h_process->info.cb_out(h_process->info.param_out, block, -1);
    }

    key = -1;
    for (; ;)
    {
        if (map_get_next(h_process->info.map_data, key, &key, &value) != 0)
            break;
        free(value);
    }

    free(h);
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
    h_process->module.start = process_start;
    h_process->module.destroy = process_destroy;
    return &h_process->module;
}


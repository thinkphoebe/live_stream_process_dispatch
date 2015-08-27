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

#include "module.h"
#include "module_utils.h"
#include "memory_pool.h"
#include "queue.h"
#include "map.h"

#define MAX_FD 65536
//#define BLOCK_SIZE 1536
#define BLOCK_SIZE 12288
#define MAX_BLOCKS (1024 * 128)

static map_t *m_fd_map_source = NULL;
static module_t *m_modules[MODULES_MAX] = { NULL };


static void on_receive_event(void *h_event, module_t *sender, int event_id, void *event_data)
{
    int i;
    sender = sender;
    for (i = 0; i < MODULES_MAX; i++)
    {
        if (m_modules[i] != NULL && m_modules[i] != sender)
            m_modules[i]->on_event(m_modules[i], event_id, event_data);
    }
}


int main()
{
    memory_pool_t *memory_pool = NULL;
    queue_t *queue_received = NULL;
    uint8_t *queue_received_buf = NULL;
    queue_t *queue_sended = NULL;
    uint8_t *queue_sended_buf = NULL;
    module_t *module_receive;
    module_t *module_process;
    module_t *module_resend;
    int queue_buf_size = MAX_BLOCKS * 24 * 2 + 512;
    module_info_t module_info;

    m_fd_map_source = map_create(MAX_FD);
    if (m_fd_map_source == NULL)
    {
    }

    queue_received_buf = (uint8_t *)malloc(queue_buf_size);
    if (queue_received_buf == NULL)
    {
    }
    queue_received = queue_create(queue_received_buf, queue_buf_size, 0);
    if (queue_received == NULL)
    {
    }

    queue_sended_buf = (uint8_t *)malloc(queue_buf_size);
    if (queue_sended_buf == NULL)
    {
    }
    queue_sended = queue_create(queue_sended_buf, queue_buf_size, 0);
    if (queue_sended == NULL)
    {
    }

    memory_pool = memory_pool_create(BLOCK_SIZE, MAX_BLOCKS);
    if (memory_pool == NULL)
    {
    }

    memset(&module_info, 0, sizeof(module_info));
    module_info.send_event = on_receive_event;
    module_info.map_data = m_fd_map_source;

    module_info.module_id = MODULE_RECEIVE;
    module_info.cb_in = (callback_in_t)memory_pool_get_block;
    module_info.cb_out = (callback_out_t)queue_put_block;
    module_info.param_in = memory_pool;
    module_info.param_out = queue_received;
    module_receive = module_receive_create(&module_info, BLOCK_SIZE, 6000);
    if (module_receive == NULL)
    {
    }
    m_modules[MODULE_RECEIVE] = module_receive;

    module_info.module_id = MODULE_RESEND;
    module_info.cb_in = (callback_in_t)queue_get_block;
    module_info.cb_out = (callback_out_t)queue_put_block;
    module_info.param_in = queue_received;
    module_info.param_out = queue_sended;
    module_resend = module_resend_create(&module_info);
    if (module_resend == NULL)
    {
    }
    m_modules[MODULE_RESEND] = module_resend;

    module_info.module_id = MODULE_PROCESS;
    module_info.cb_in = (callback_in_t)queue_get_block;
    module_info.cb_out = (callback_out_t)memory_pool_put_block;
    module_info.param_in = queue_sended;
    module_info.param_out = memory_pool;
    module_process = module_process_create(&module_info);
    if (module_process == NULL)
    {
    }
    m_modules[MODULE_PROCESS] = module_process;

    module_receive->start(module_receive);
    module_resend->start(module_resend);
    module_process->start(module_process);

    getchar();
    return 0;
}


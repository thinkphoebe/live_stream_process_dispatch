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
#include "handler_receive.h"
#include "handler_process.h"
#include "handler_resend.h"
#include "memory_pool.h"
#include "queue.h"

#define MAX_FD 65536
//#define BLOCK_SIZE 1536
#define BLOCK_SIZE 12288
#define MAX_BLOCKS (1024 * 128)

static fd_map_t *m_fd_map_source = NULL;
handler_t *handler_receive;
handler_t *handler_process;
handler_t *handler_resend;


static void on_receive_event(int type, int fd)
{
    //source_info_t *info;

    //type: 0->connected, 1->disconnected
    if (type == 0)
    {
        //info = (source_info_t *)malloc(sizeof(source_info_t));
        //if (info == NULL)
        //{
        //    return;
        //}
        //memset(info, 0, sizeof(*info));
        //info->fd = fd;

        //if (fd_map_add(m_fd_map_source, fd, info) != 0)
        //{
        //}

        handler_resend_on_event(handler_resend, type, fd);
    }
    else if (type == 1)
    {
        handler_resend_on_event(handler_resend, type, fd);

        //info = fd_map_get(m_fd_map_source, fd);

        //if (fd_map_remove(m_fd_map_source, fd) != 0)
        //{
        //}

        //free(info);
    }
}


int main()
{
    memory_pool_t *memory_pool = NULL;
    queue_t *queue_received = NULL;
    uint8_t *queue_received_buf = NULL;
    queue_t *queue_sended = NULL;
    uint8_t *queue_sended_buf = NULL;
    int queue_buf_size = MAX_BLOCKS * 24 * 2 + 512;

    m_fd_map_source = fd_map_create(MAX_FD);
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

    handler_receive = handler_receive_create(BLOCK_SIZE, 6000, m_fd_map_source, on_receive_event);
    if (handler_receive == NULL)
    {
    }
    handler_receive->set_callback_in(handler_receive, (callback_in_t)memory_pool_get_block, memory_pool);
    handler_receive->set_callback_out(handler_receive, (callback_out_t)queue_put_block, queue_received);

    handler_resend = handler_resend_create(BLOCK_SIZE, m_fd_map_source);
    if (handler_resend == NULL)
    {
    }
    handler_resend->set_callback_in(handler_resend, (callback_in_t)queue_get_block, queue_received);
    handler_resend->set_callback_out(handler_resend, (callback_out_t)queue_put_block, queue_sended);

    handler_process = handler_process_create(BLOCK_SIZE);
    if (handler_process == NULL)
    {
    }
    handler_process->set_callback_in(handler_process, (callback_in_t)queue_get_block, queue_sended);
    handler_process->set_callback_out(handler_process, (callback_out_t)memory_pool_put_block, memory_pool);

    handler_receive->start(handler_receive);
    handler_resend->start(handler_resend);
    handler_process->start(handler_process);

    getchar();
    return 0;
}


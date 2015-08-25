/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#ifndef __HANDLER_COMMON_H__
#define __HANDLER_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "queue.h"
#include "memory_pool.h"

typedef void* (*callback_in_t)(void *param, int wait);
typedef int (*callback_out_t)(void *param, void *block, int wait);

typedef struct _handler handler_t;
struct _handler
{
    int (*start)(handler_t *h);
    void (*destroy)(handler_t *h);

    void (*set_callback_in)(handler_t *h, callback_in_t callback, void *param);
    void (*set_callback_out)(handler_t *h, callback_out_t callback, void *param);
};

void* queue_get_block(queue_t *queue, int wait);
int queue_put_block(queue_t *queue, void *block, int wait);

void* memory_pool_get_block(memory_pool_t *memory_pool, int wait);
int memory_pool_put_block(memory_pool_t *memory_pool, void *block, int wait);


#include <sys/socket.h>
#include <netdb.h>

typedef struct _source_info
{
    int fd;
    void *info_resend;
    //TODO: add other infos
} source_info_t;

typedef struct _fd_map fd_map_t;

fd_map_t* fd_map_create(int max_fd);
void fd_map_destroy(fd_map_t *h);

int fd_map_add(fd_map_t *h, int fd, void *info);
int fd_map_remove(fd_map_t *h, int fd);
void* fd_map_get(fd_map_t *h, int fd);


#ifdef __cplusplus
}
#endif

#endif // __HANDLER_COMMON_H__


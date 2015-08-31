/**
 * @brief some utility functions
 * @author Sunniwell
 * @date 2015-08-27 created
 */

#ifndef __MODULE_UTILS_H__
#define __MODULE_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "module.h"

int set_module_data(modules_data_t *d, int module_id, void *data);
void* get_module_data(modules_data_t *d, int module_id);

//timeout in ms, -1 for unlimited or > 0
void* queue_get_block(queue_t *queue, int timeout);
int queue_put_block(queue_t *queue, void *block, int timeout);

void* memory_pool_get_block(memory_pool_t *memory_pool, int timeout);
int memory_pool_put_block(memory_pool_t *memory_pool, void *block, int timeout);

int serve_socket(int port);
int set_socket_nonblocking(int fd);


#ifdef __cplusplus
}
#endif

#endif // __MODULE_UTILS_H__


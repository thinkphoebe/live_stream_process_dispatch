/**
 * @brief 
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

void* queue_get_block(queue_t *queue, int wait);
int queue_put_block(queue_t *queue, void *block, int wait);

void* memory_pool_get_block(memory_pool_t *memory_pool, int wait);
int memory_pool_put_block(memory_pool_t *memory_pool, void *block, int wait);

int create_and_bind(const char *port);
int make_socket_non_blocking (int sfd);


#ifdef __cplusplus
}
#endif

#endif // __MODULE_UTILS_H__


/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#ifndef __MODULE_H__
#define __MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "memory_pool.h"
#include "queue.h"
#include "map.h"

#define MODULES_MAX 5

#define EVENT_SRC_ADD 0
#define EVENT_SRC_DEL 1

typedef void* (*callback_in_t)(void *param, int wait);
typedef int (*callback_out_t)(void *param, void *block, int wait);

typedef struct _module module_t;

struct _module
{
    int (*start)(module_t *h);
    void (*destroy)(module_t *h);
    void (*on_event)(module_t *h, int event_id, void *event_data);
};

typedef struct _module_info 
{
    int module_id; //[0, MODULES_MAX), should be unique for every module
    map_t *map_data;

    callback_in_t cb_in;
    callback_out_t cb_out;
    void *param_in;
    void *param_out;

    void *h_event;
    void (*send_event)(void *h_event, module_t *sender, int event_id, void *event_data);
} module_info_t;

typedef struct _modules_data
{
    int key;
    void *modules[MODULES_MAX];
} modules_data_t;


#define MODULE_RECEIVE 0
#define MODULE_RESEND 1
#define MODULE_PROCESS 2

module_t* module_receive_create(const module_info_t *info, int block_size, int port);
module_t* module_resend_create(const module_info_t *info);
module_t* module_process_create(const module_info_t *info);


#ifdef __cplusplus
}
#endif

#endif // __MODULE_H__


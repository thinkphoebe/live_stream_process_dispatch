/**
 * @brief common header for modules
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

#define MODULES_MAX 3

#define MODULE_RECEIVE 0
#define MODULE_RESEND  1
#define MODULE_PROCESS 2

//block fd为0时为event，后面4字节不是data size，是event id，再后面数据随具体event定义，但应注意不超过block_size大小
#define BLOCK_FD(buf)           (*(int32_t *)buf)
#define BLOCK_DATA_SIZE(buf)    (*(int32_t *)(buf + 4))
#define BLOCK_DATA(buf)         (buf + 8)

#define EVENT_SRC_ADD 0 //后面4字节为fd
#define EVENT_SRC_DEL 1 //后面4字节为fd

typedef void* (*callback_in_t)(void *param, int timeout);
typedef int (*callback_out_t)(void *param, void *block, int timeout);

typedef struct _module module_t;

struct _module
{
    int (*start)(module_t *h);
    void (*destroy)(module_t *h);
};

typedef struct _module_info 
{
    int module_id; //[0, MODULES_MAX), should be unique for every module
    map_t *map_data;

    callback_in_t cb_in;
    callback_out_t cb_out;
    void *param_in;
    void *param_out;
} module_info_t;

typedef struct _modules_data
{
    int key;
    void *modules[MODULES_MAX];
} modules_data_t;


//ATTENTION: 注意pipe line的规划，modules_data_t在module receive中分配(第一个节点)，
//在module process中释放(最后一个节点)
module_t* module_receive_create(const module_info_t *info, int block_size, int port);
module_t* module_resend_create(const module_info_t *info, int port);
module_t* module_process_create(const module_info_t *info);


#ifdef __cplusplus
}
#endif

#endif // __MODULE_H__


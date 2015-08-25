/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#ifndef __HANDLER_RECEIVE_H__
#define __HANDLER_RECEIVE_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "handler_common.h"

//type: 0->connected, 1->disconnected
typedef void (*handler_receive_event_t)(int type, int fd);

handler_t* handler_receive_create(int block_size, int port, fd_map_t *fd_map_source, handler_receive_event_t on_event);


#ifdef __cplusplus
}
#endif

#endif // __HANDLER_RECEIVE_H__


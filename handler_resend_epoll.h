/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#ifndef __HANDLER_RESEND_H__
#define __HANDLER_RESEND_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "handler_common.h"

handler_t* handler_resend_create(int block_size, fd_map_t *fd_map);

void handler_resend_on_event(handler_t *h, int type, int fd);


#ifdef __cplusplus
}
#endif

#endif // __HANDLER_RESEND_H__


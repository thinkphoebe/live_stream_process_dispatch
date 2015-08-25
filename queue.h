/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

#ifndef __QUEUE_H__
#define __QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _queue queue_t;

queue_t* queue_create(uint8_t *buf, int size, int block);
void queue_destroy(queue_t *qobj);

int queue_record_newbegin(queue_t *qobj);
int queue_reset(queue_t *qobj);

int queue_get_writebuf(queue_t *qobj, uint8_t **buf, int *size);
int queue_write_complete(queue_t *qobj, uint8_t *buf, int size, int skip);

int queue_get_readbuf(queue_t *qobj, uint8_t **buf, int *size);
int queue_read_complete(queue_t *qobj);


#ifdef __cplusplus
}
#endif

#endif // __QUEUE_H__


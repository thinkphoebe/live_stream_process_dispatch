/**
 * @brief buffer queue
 * @author Ye Shengnan
 * @date 2015-08-17 created

|-------- 4 bytes blocksize ---------------|-------------- 4 bytes offset ------------------|
|-------- 4 bytes playloadsize ------------|-------------- 4 bytes revered -----------------| 
|-------------------------------------- playload -------------------------------------------|
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <semaphore.h>
#include "queue.h"

struct _queue
{
    int size;
    uint8_t *real_buf;
    uint8_t *buf;
    uint8_t *buf_end;

    uint8_t *head;
    uint8_t *tail;
    uint8_t *new_begin;

    int block; //空间不足时是否等待，默认为等待
    sem_t sem;
};

#define ALIGN 8 //必须为2^n
#define MEMBLOCK_HEADER_LEN 16 //内存块头信息长度

#define BLOCK_SIZE(buf)     (*(int32_t *) buf)
#define PAYLOAD_OFFSET(buf) (*(uint32_t*)(buf + 4))
#define PAYLOAD_SIZE(buf)   (*(uint32_t*)(buf + 8))


queue_t* queue_create(uint8_t *buf, int size, int block)
{
    queue_t *qobj;
    printf("BEGIN..., size:%dk\n", size / 1024);
    
    qobj = (queue_t *)malloc(sizeof(queue_t));
    if (qobj == NULL) 
        goto FAIL;
    memset(qobj, 0, sizeof(queue_t));

    qobj->size = size;
    qobj->real_buf = buf;
    qobj->buf = (uint8_t *)((uint64_t)(buf + ALIGN - 1) & ~(uint64_t)(ALIGN - 1));
    qobj->buf_end = buf + size;

    qobj->head = qobj->buf;
    qobj->tail = qobj->buf;

    qobj->block = block;

    if (sem_init(&qobj->sem, 0, 0) == -1)
    {
        printf("sem_init FAILED! %s", strerror(errno));
        goto FAIL;
    }

    printf("OK!\n");
    return qobj;

FAIL:
    printf("FAILED!\n");
    if (qobj != NULL)
        free(qobj);
    return NULL;
}


void queue_destroy(queue_t *qobj)
{
    printf("qobj:0x%p\n", qobj);
    if (qobj != NULL)
    {
        int val;
        sem_getvalue(&qobj->sem, &val);
        if (val < 1)
            sem_post(&qobj->sem);
        sem_destroy(&qobj->sem);
        free(qobj);
    }
}


int queue_record_newbegin(queue_t *qobj)
{
    printf("qobj:0x%p\n", qobj);
    qobj->new_begin = qobj->tail;

    //ATTENTION
    int val;
    sem_getvalue(&qobj->sem, &val);
    if (val < 1)
        sem_post(&qobj->sem);
    return 0;
}


int queue_reset(queue_t *qobj)
{
    printf("qobj:0x%p\n", qobj);

    if (qobj->new_begin == NULL)
    {
        qobj->head = qobj->tail;
    } 
    else
    {
        qobj->head = qobj->new_begin;
        qobj->new_begin = NULL;
    }
    
    //ATTENTION 
    int val;
    sem_getvalue(&qobj->sem, &val);
    if (val < 1)
        sem_post(&qobj->sem);
    return 0;
}


int queue_get_writebuf(queue_t *qobj, uint8_t **buf, int *size)
{
    int need_size;
    uint8_t *head;

    //ATTENTION
    if (*size <= 0 || qobj == NULL)
        return -1;

    need_size = MEMBLOCK_HEADER_LEN + *size + ALIGN - 1 + MEMBLOCK_HEADER_LEN/*为尾部预留一个头的位置*/;

    if (need_size > qobj->buf_end - qobj->buf)
    {
        int newsize;
        need_size = qobj->buf_end - qobj->buf;
        newsize = need_size - MEMBLOCK_HEADER_LEN - ALIGN + 1 - MEMBLOCK_HEADER_LEN;
        printf("require size [%d] too big, truncated to %d!\n", *size, newsize);
        *size = newsize;
    }

AGAIN:
    head = qobj->head;
    //从头部重新开始
    if (qobj->tail >= head && qobj->tail + need_size >= qobj->buf_end)
    {   
        if (head == qobj->buf)
            goto WOF;

        BLOCK_SIZE(qobj->tail) = -1; //用-1标记为需跳转至头
        qobj->tail = qobj->buf;
        goto AGAIN;
    }

    //==时不写入保证满时head != tail
    if (qobj->tail < head && (qobj->tail + need_size >= head))
        goto WOF;

    if (qobj->new_begin != NULL)
    {
        if (qobj->tail < qobj->new_begin && (qobj->tail + need_size >= qobj->new_begin))
        {
            printf("EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE\n");
            printf("cycle to new_begin, wait for strmqueue_reset...\n");
            goto WOF;
        }
    }

    *buf = qobj->tail + MEMBLOCK_HEADER_LEN;
    return 0;

WOF: //wait or fail
    if (qobj->block && qobj->new_begin == NULL)
    {
		sem_wait(&qobj->sem);
        goto AGAIN;
    } 
    else
    {
        *buf = 0;
        *size = 0;
        return 0;
    }
}


int queue_write_complete(queue_t *qobj, uint8_t *buf, int size, int skip)
{
    int block_size;

	if ( qobj == NULL )
	{
		printf("stream queue is NULL\n");
		return -1;
	}
    if (buf != qobj->tail + MEMBLOCK_HEADER_LEN)
    {
        printf("mismatched GET_BUF and PUT_BUF\n");
        return -1;
    }

    block_size = (MEMBLOCK_HEADER_LEN + size + skip + ALIGN - 1) & ~(ALIGN - 1);
    BLOCK_SIZE(qobj->tail) = block_size;
    PAYLOAD_OFFSET(qobj->tail) = MEMBLOCK_HEADER_LEN + skip;
    PAYLOAD_SIZE(qobj->tail) = size;
    qobj->tail += block_size;
    return 0;
}


int queue_get_readbuf(queue_t *qobj, uint8_t **buf, int *size)
{
    if (qobj == NULL || qobj->head == qobj->tail)
        goto NODATA;

    if (BLOCK_SIZE(qobj->head) == -1) //跳到开头
    {
        qobj->head = qobj->buf;
        if (qobj->head == qobj->tail)
            goto NODATA;
    }

    *buf = qobj->head + PAYLOAD_OFFSET(qobj->head);
    *size = PAYLOAD_SIZE(qobj->head);
    return 0;

NODATA:
    *size = 0;
    return -1;
}


int queue_read_complete(queue_t *qobj)
{
	if (qobj == NULL)
	{
		printf("stream queue is null\n");
		return -1;
	}
    int block_size = BLOCK_SIZE(qobj->head);
    qobj->head += block_size;
    //TODO 被reset打断导致对tail错误修改的情况?

    if (qobj->block)
    {
        int val;
        sem_getvalue(&qobj->sem, &val);
        if (val < 1)
            sem_post(&qobj->sem);
    }
    return 0;
}


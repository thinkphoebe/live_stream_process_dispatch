/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include "queue.h"
#include "memory_pool.h"

#define ARRAY_BLOCKS 512

struct _memory_pool
{
    int block_size;
    int max_blocks;

    int arrays_count;
    int arrays_allocated;
    void **p_arrays;

    void* unused_blocks[ARRAY_BLOCKS];
    int unused_blocks_count;

    queue_t *queue;
    uint8_t *queue_buf;
};


memory_pool_t* memory_pool_create(int block_size, int max_blocks)
{
    memory_pool_t *h = (memory_pool_t *)malloc(sizeof(memory_pool_t));
    if (h == NULL)
    {
        printf("malloc FAILED! %d\n", (int)sizeof(memory_pool_t));
        return NULL;
    }
    memset(h, 0, sizeof(*h));

    h->block_size = block_size;
    h->arrays_count = ((max_blocks + ARRAY_BLOCKS - 1) / ARRAY_BLOCKS);
    h->max_blocks = h->arrays_count * ARRAY_BLOCKS;

    h->p_arrays = malloc(h->arrays_count * sizeof(void *));
    if (h->p_arrays == NULL)
    {
        goto FAIL;
    }
    memset(h->p_arrays, 0, h->arrays_count * sizeof(void *));

    int queue_buf_size = h->max_blocks * (16 + 8) + 256;
    h->queue_buf = (uint8_t *)malloc(queue_buf_size);
    if (h->queue_buf == NULL)
    {
        printf("malloc FAILED! %d\n", queue_buf_size);
        goto FAIL;
    }

    h->queue = queue_create(h->queue_buf, queue_buf_size, 0);
    if (h->queue == NULL)
        goto FAIL;

    return h;

FAIL:
    memory_pool_destroy(h);
    return NULL;
}


void memory_pool_destroy(memory_pool_t *h)
{
    int i;
    for (i = 0; i < h->arrays_count; i++)
        free(h->p_arrays[i]);
    free(h->p_arrays);

    free(h->queue_buf);
    free(h);
}


void* memory_pool_alloc(memory_pool_t *h)
{
    void *block;
    uint8_t *queue_buf;
    int queue_buf_size;

    if (queue_get_readbuf(h->queue, &queue_buf, &queue_buf_size) == 0)
    {
        block = *(void **)queue_buf;
        //memcpy(&block, queue_buf, sizeof(void *));
        queue_read_complete(h->queue);
        return block;
    }

    if (h->unused_blocks_count > 0)
    {
        h->unused_blocks_count -= 1;
        return h->unused_blocks[h->unused_blocks_count];
    }

    if (h->arrays_allocated < h->arrays_count)
    {
        void *array = malloc(h->block_size * ARRAY_BLOCKS);
        int i;
        if (array == NULL)
        {
            printf("malloc FAILED! %d\n", h->block_size * ARRAY_BLOCKS);
            return NULL;
        }
        for (i = 1; i < ARRAY_BLOCKS; i++)
            h->unused_blocks[i] = array + i * h->block_size;
        h->unused_blocks_count = ARRAY_BLOCKS - 1;

        h->p_arrays[h->arrays_allocated] = array;
        h->arrays_allocated += 1;
        return array;
    }

    //printf("max blocks [%d] reached\n", h->max_blocks);
    return NULL;
}


int memory_pool_free(memory_pool_t *h, void *block)
{
    uint8_t *queue_buf;
    int queue_buf_size = sizeof(void *);

    if (queue_get_writebuf(h->queue, &queue_buf, &queue_buf_size) != 0)
    {
        printf("write block to queue FAILED!\n");
        return -1;
    }

    //memcpy(queue_buf, &block, sizeof(void *));
    *(void **)queue_buf = block;
    queue_write_complete(h->queue, queue_buf, sizeof(void *), 0);
    return 0;
}


#ifdef MEMORY_POOL_TEST
static queue_t *m_queue;
static memory_pool_t *m_pool;
static int64_t m_write_count = 0;
static int64_t m_check_count = 0;

static void* thread_write(void *arg)
{
    uint8_t *buf;
    int rnd;
    int i;

    uint8_t *queue_buf;
    int queue_buf_size = sizeof(void *);

    for (; ;)
    {
        buf = memory_pool_alloc(m_pool);
        if (buf == NULL)
        {
            usleep(10);
            continue;
        }

        rnd = random() % 256;
        for (i = 0; i < 1024; i++)
            buf[i] = (rnd + i) % 256;


        for (; ;)
        {
            if (queue_get_writebuf(m_queue, &queue_buf, &queue_buf_size) != 0)
            {
                //printf("write block to queue FAILED!\n");
                usleep(10);
                continue;
            }

            memcpy(queue_buf, &buf, sizeof(void *));
            queue_write_complete(m_queue, queue_buf, sizeof(void *), 0);
            break;
        }

        m_write_count += 1;
        if (m_write_count % 10000 == 0)
            printf("write %"PRId64" times\n", m_write_count);
    }

    return NULL;
}

static void* thread_check(void *arg)
{
    uint8_t *buf;
    int rnd;
    int i;

    uint8_t *queue_buf;
    int queue_buf_size;

    for (; ;)
    {
        for (; ;)
        {
            if (queue_get_readbuf(m_queue, &queue_buf, &queue_buf_size) != 0)
            {
                //printf("read block to from FAILED!\n");
                usleep(10);
                continue;
            }

            memcpy(&buf, queue_buf, sizeof(void *));
            queue_read_complete(m_queue);
            break;
        }

        rnd = buf[0];
        for (i = 1; i < 1024; i++)
        {
            if (buf[i] != (rnd + i) % 256)
            {
                printf("check FAILED!\n");
                printf("write count:%"PRId64", check count:%"PRId64"\n", m_write_count, m_check_count);
                exit(-1);
            }
            
        }

        memory_pool_free(m_pool, buf);

        m_check_count += 1;
        if (m_check_count % 10000 == 0)
            printf("check %"PRId64" times\n", m_check_count);
    }

    return NULL;
}

int main()
{
    pthread_t thrd_write;
    pthread_t thrd_check;
    uint8_t *queue_buf;

    queue_buf = (uint8_t *)malloc(1024 * 5120);
    m_queue = queue_create(queue_buf, 1024 * 5120, 0);

    m_pool = memory_pool_create(1024, 50000);

    if (pthread_create(&thrd_write, NULL, thread_write, 0) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }

    if (pthread_create(&thrd_check, NULL, thread_check, 0) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }

    getchar();
    printf("write count:%"PRId64", check count:%"PRId64"\n", m_write_count, m_check_count);
    return 0;
}
#endif

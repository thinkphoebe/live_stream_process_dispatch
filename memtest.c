/**
 * @brief memory test program for sunniwell streamqueue
 * @author Ye Shengnan
 * @date 2012-06-01 created
 *
 * Sunniwell media framework use a none block queue for media stream buffering.
 * Generally, the feeding thread write a stream block and set the marker flag.
 * When the reading thread found there is a stream block by checking the marker flag,
 * it read the stream block content. The marker flag is set to volatile, but the whole
 * stream buffer not. On some platform, the reading thread may read garbage data
 * immediately after the feeding thread write. This program is designed to check this case.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#define TOTAL_SIZE (1024 * 1024 * 20)

static pthread_t m_thrd_feed;
static pthread_t m_thrd_read;

static char *m_mem;
static volatile uint32_t m_curr_num = 0;
static volatile uint32_t m_curr_pos = 0;
static volatile int m_feed_size = 0;
static volatile int m_feed_flag;
static volatile int m_read_flag;
static int64_t m_read_count = 0;
static int64_t m_failed_count = 0;


static void* thread_feed(void *arg)
{
    int i;
    volatile uint32_t *p;
    for (; ;)
    {
        if (m_read_flag != 1)
        {
            usleep(1);
            continue;
        }
        m_read_flag = 0;

        m_feed_size = 1024 + random() % 16000;
        if (m_curr_pos + m_feed_size * 4 > TOTAL_SIZE)
            m_curr_pos = 0;
        if (m_curr_num >= 0xFFFFFFFF - TOTAL_SIZE)
            m_curr_num = 0;
        p = (uint32_t *)(m_mem + m_curr_pos);
        for (i = 0; i < m_feed_size; i++)
        {
            *p = m_curr_num + i * m_feed_size;
            p++;
        }

        m_feed_flag = 1;
    }

    return NULL;
}


static void* thread_read(void *arg)
{
    int i;
    volatile uint32_t *p;
    uint32_t readed_data_1, readed_data_2;
    for (; ;)
    {
        if (m_feed_flag != 1)
        {
            usleep(1);
            continue;
        }
        m_feed_flag = 0;

#if 0
        p = (uint32_t *)(m_mem + m_curr_pos);
        for (i = 0; i < m_feed_size; i++)
        {
            readed_data_1 = *p;
            if (readed_data_1 != m_curr_num + i * m_feed_size)
            {
                printf("[%d] AAAAAAAAAAAAAAAAAAAAAAAAA\n", __LINE__);
                printf("[%d] read data invalid, read_count:%lld, failed_count:%lld\n", __LINE__, m_read_count, m_failed_count);
                printf("[%d] curr_pos:%u, curr_num:0x%x, feed_size:%d, i:%d\n", __LINE__, m_curr_pos, m_curr_num, m_feed_size, i);
                usleep(100000);
                readed_data_2 = *p;
                printf("[%d] readed_data_1:0x%x, readed_data_2:0x%x\n", __LINE__, readed_data_1, readed_data_2);
                printf("[%d] BBBBBBBBBBBBBBBBBBBBBBBBB\n", __LINE__);
                m_failed_count++;
            }
            p++;
        }
#else
        //check most recently writed first
        p = (uint32_t *)(m_mem + m_curr_pos + (m_feed_size - 1) * 4);
        for (i = m_feed_size - 1; i >= 0; i--)
        {
            readed_data_1 = *p;
            if (readed_data_1 != m_curr_num + i * m_feed_size)
            {
                printf("[%d] AAAAAAAAAAAAAAAAAAAAAAAAA\n", __LINE__);
                printf("[%d] read data invalid, read_count:%lld, failed_count:%lld\n", __LINE__, m_read_count, m_failed_count);
                printf("[%d] curr_pos:%u, curr_num:0x%x, feed_size:%d, i:%d\n", __LINE__, m_curr_pos, m_curr_num, m_feed_size, i);
                usleep(100000);
                readed_data_2 = *p;
                printf("[%d] readed_data_1:0x%x, readed_data_2:0x%x\n", __LINE__, readed_data_1, readed_data_2);
                printf("[%d] BBBBBBBBBBBBBBBBBBBBBBBBB\n", __LINE__);
                m_failed_count++;
            }
            p--;
        }
#endif

        m_curr_pos += (m_feed_size * 4);
        m_curr_num += m_feed_size;

        m_read_count++;
        if (m_read_count % 5000 == 0)
            printf("read count:%lld\n", m_read_count);

        m_read_flag = 1;
    }

    return NULL;
}


int main()
{
    m_mem = (char *)malloc(TOTAL_SIZE);
    if (m_mem == NULL)
    {
        printf("[%d] malloc FAILED!\n", __LINE__);
        return -1;
    }

    if (pthread_create(&m_thrd_feed, NULL, thread_feed, 0) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }

    if (pthread_create(&m_thrd_read, NULL, thread_read, 0) != 0)
    {
        printf("[%d] pthread_create FAILED!\n", __LINE__);
        return -1;
    }

    m_read_flag = 1;
    m_feed_flag = 0;

    getchar();
    printf("[%d] read_count:%lld, failed_count:%lld\n", __LINE__, m_read_count, m_failed_count);
    return 0;
}

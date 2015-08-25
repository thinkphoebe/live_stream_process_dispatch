/**
 * @brief 
 * @author Ye Shengnan
 * @date 2015-08-17 created
 */

typedef struct _memory_pool memory_pool_t;

memory_pool_t* memory_pool_create(int block_size, int max_blocks);
void memory_pool_destroy(memory_pool_t *h);

void* memory_pool_alloc(memory_pool_t *h);
int memory_pool_free(memory_pool_t *h, void *block);


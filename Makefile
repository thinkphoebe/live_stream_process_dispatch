default:
	gcc -g -Wall -o demo_app memory_pool.c queue.c map.c module_utils.c module_receive.c module_resend.c module_process.c demo_app.c -lpthread

memtest:
	gcc -o memtest memtest.c -lpthread 

memory_pool: memory_pool.c queue.c
	gcc -o memory_pool memory_pool.c queue.c -lpthread -DMEMORY_POOL_TEST

default:
	gcc -g -Wall -o cds_demo memory_pool.c queue.c handler_common.c handler_receive.c handler_resend_epoll.c handler_process.c main.c -lpthread
	#gcc -g -Wall -o cds_demo memory_pool.c queue.c handler_common.c handler_receive.c handler_resend.c handler_process.c main.c -lpthread

memtest:
	gcc -o memtest memtest.c -lpthread 

memory_pool: memory_pool.c queue.c
	gcc -o memory_pool memory_pool.c queue.c -lpthread -DMEMORY_POOL_TEST

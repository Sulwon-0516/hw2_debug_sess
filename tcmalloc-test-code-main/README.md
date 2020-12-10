# tcmalloc-test-code

1. Compile
 * gcc eval.c <path-to-your-tc_malloc.c> -o <path-to-binary> -I<path-to-your-tc_malloc.h> -lpthread <-lm>
 * You should include -lm, if you use math libarary. For example,
 * gcc eval.c tc_malloc.c -o 20205008 -Iinclude/ -lpthread -lm

2. Usage
 * ./\<bin-of-your-impl> \<number-of-thread>
 * For example,
 * ./20205008 200

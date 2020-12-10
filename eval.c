#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "tc_malloc.h"

long * res_a;
long * res_b;
long * res_c;
long * res_d;
long * res_e;
long * res_f;

/* Test case a: Allocates 100 small objects size of 8 bytes */
void *test1(void *data){
    long tid = (long) data;
    void* ptr[100];
    int iter;
    clock_t start = clock();
    //void * start_addr = tc_thread_init();
    for (iter = 0; iter < 100; iter++)
        ptr[iter] = tc_malloc(8);
    clock_t clocks = clock() - start;
    *(res_a + tid) = clocks;
    for (iter = 0; iter < 100; iter++)
        tc_free(ptr[iter]);

    printf("test1 is done\n");
}

void *test2(void *data){
    long tid = (long) data;
    void* ptr[200];
    int iter;
    clock_t start, clocks;
    //void * start_addr = tc_thread_init();
/* Test case b: Allocates 100 small objects size of 128 bytes */
    start = clock();
    for (iter = 0; iter < 100; iter++)
        ptr[iter] = tc_malloc(128);
    clocks = clock() - start;
    *(res_b + tid) = clocks;
    
/* Test case c: Allocates 100 small objects size of 1024 bytes */
    start = clock();
    for (iter = 100; iter < 200; iter++)
        ptr[iter] = tc_malloc(1024);
    clocks = clock() - start;
    *(res_c + tid) = clocks;

/* Test case e: Frees 200 small objects */
    start = clock();
    for (iter = 0; iter < 200; iter++){
        tc_free(ptr[iter]);
    }
    clocks = clock() - start;
    *(res_e + tid) = clocks;

    printf("test2 is done\n");
}

void *test3(void *data){
    long tid = (long) data;
    void* ptr[10];
    int iter;
    clock_t start, clocks;
    //void * start_addr = tc_thread_init();
/* Test case d: Allocates 10 large objects size of 64 Kbytes */
    start = clock();
    for (iter = 0; iter < 10; iter++)
        ptr[iter] = tc_malloc(64 * 1024);
    clocks = clock() - start;
    res_d[tid] = clocks;
    
/* Test case f: Frees 10 large objects */
    start = clock();
    for (iter = 0; iter < 10; iter++){
        tc_free(ptr[iter]);
    }
    clocks = clock() - start;
    res_f[tid] = clocks;

    printf("test3 is done\n");
}

int main(int argc, char * argv[]){
    pthread_t* p_thread;
    int thr_id;
    long iter, testidx;
    clock_t sum;
    void* start_addr = tc_central_init();
    if ( argc != 2 ){
        printf("Usage: %s <thread_num>\n", argv[0]);
        return 1;
    }
    int THREAD_NUM = atoi(argv[1]);
    p_thread = (pthread_t*)malloc(sizeof(pthread_t)*THREAD_NUM);
    res_a = (long*)malloc(sizeof(long)*THREAD_NUM);
    res_b = (long*)malloc(sizeof(long)*THREAD_NUM);
    res_c = (long*)malloc(sizeof(long)*THREAD_NUM);
    res_d = (long*)malloc(sizeof(long)*THREAD_NUM);
    res_e = (long*)malloc(sizeof(long)*THREAD_NUM);
    res_f = (long*)malloc(sizeof(long)*THREAD_NUM);
/* Test 1 */
    for (iter = 0; iter < THREAD_NUM; iter++){
        thr_id = pthread_create(p_thread + iter, NULL, test1, (void *) iter);
        if (thr_id < 0){
            perror("Thread create error : ");
            exit(0);
        }
    }
    for (iter = 0; iter < THREAD_NUM; iter++){
        pthread_join(*(p_thread + iter), (void**) NULL);
    }
    
/* Test 2 */
    for (iter = 0; iter < THREAD_NUM; iter++){
        thr_id = pthread_create(p_thread + iter, NULL, test2, (void *) iter);
        if (thr_id < 0){
            perror("Thread create error : ");
            exit(0);
        }
    }
    for (iter = 0; iter < THREAD_NUM; iter++){
        pthread_join(*(p_thread + iter), (void**) NULL);
    }
    
/* Test 3 */
    for (iter = 0; iter < THREAD_NUM; iter++){
        thr_id = pthread_create(p_thread + iter, NULL, test3, (void *) iter);
        if (thr_id < 0){
            perror("Thread create error : ");
            exit(0);
        }
    }
    for (iter = 0; iter < THREAD_NUM; iter++){
        pthread_join(*(p_thread + iter), (void**) NULL);
    }
    
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_a + iter);
    printf("%f, ", (double) sum / CLOCKS_PER_SEC);
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_b + iter);
    printf("%f, ", (double) sum / CLOCKS_PER_SEC);
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_c + iter);
    printf("%f, ", (double) sum / CLOCKS_PER_SEC);
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_d + iter);
    printf("%f, ", (double) sum / CLOCKS_PER_SEC);
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_e + iter);
    printf("%f, ", (double) sum / CLOCKS_PER_SEC);
    sum=0;
    for (iter = 0; iter < THREAD_NUM; iter++) sum += *(res_f + iter);
    printf("%f\n", (double) sum / CLOCKS_PER_SEC);

    free(p_thread);
    free(res_a);
    free(res_b);
    free(res_c);
    free(res_d);
    free(res_e);
    free(res_f);

    return 0;
}

#ifndef TC_MALLOC_H
#define TC_MALLOC_H
#include <stdio.h>
#include <pthread.h>

pthread_spinlock_t s_lock;
void * meta_addr;
void * meta_addr_init;
void * meta_addr_end;
const int num_ind[3];
const int batch_size[3];
const int size_list_1[8];
const int size_list_2[31];
const int size_list_3[120];

struct Radix_node;
struct Radix_root;
struct thread_free_list;
struct Thread_cache;
struct CentralFreeList;
struct Central_cache;
struct Page_Heap;
struct Meta;

struct Meta* meta_data;
//functions that called by outside.

struct Radix_root * Radix_tree_init();
struct Span * search_tree(unsigned long long int , struct Radix_root * );
void add_one_node(unsigned long long int , struct Span * ,struct Radix_root * );
void add_Span(struct Span * , struct Radix_root * );
struct Span * merge_spans(struct Span * , struct Span * );
struct Span * alloc_pages(int , size_t );
void * alloc_small_1(size_t ,struct Thread_cache * );
void * alloc_small_2(size_t , struct Thread_cache * );
void * alloc_small_3(size_t , struct Thread_cache * );
void tc_garbage_collect(struct Thread_cache * , int , int );

void * tc_central_init();
void * tc_thread_init();
void * tc_malloc(size_t );
void tc_free(void *);
#endif
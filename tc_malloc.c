#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>


//when 64bit, 
#define PAGE_SIZE 4096

pthread_spinlock_t s_lock;
void * meta_addr = NULL;
void * meta_addr_init = NULL;
void * meta_addr_end = NULL;
struct Meta* meta_data = NULL;
const int num_ind[3]={8,31,120};
const int batch_size[3]={100,100,100};
const int size_list_1[8]={8,16,24,32,40,48,56,64};
const int size_list_2[31]={128,192,256,320,384,448,512,576,640,704,768,832,896,960,1024,1088,1152,1216,1280,1344,1408,1472,1536,1600,1664,1728,1792,1856,1920,1984,2048};
const int size_list_3[120]={2304,2560,2816,3072,3328,3584,3840,4096,4352,4608,4864,5120,5376,5632,5888,6144,6400,6656,6912,7168,7424,7680,7936,8192,8448,8704,8960,9216,9472,9728,9984,10240,10496,10752,11008,11264,11520,11776,12032,12288,12544,12800,13056,13312,13568,13824,14080,14336,14592,14848,15104,15360,15616,15872,16128,16384,16640,16896,17152,17408,17664,17920,18176,18432,18688,18944,19200,19456,19712,19968,20224,20480,20736,20992,21248,21504,21760,22016,22272,22528,22784,23040,23296,23552,23808,24064,24320,24576,24832,25088,25344,25600,25856,26112,26368,26624,26880,27136,27392,27648,27904,28160,28416,28672,28928,29184,29440,29696,29952,30208,30464,30720,30976,31232,31488,31744,32000,32256,32512,32768};



struct Radix_node{
    void ** p_list[64];
    int cnt;
    int level;
};

struct Radix_root{
    int height;
    unsigned long long int tot_cnt;
    struct Radix_node * root_node;

};

struct thread_free_list{
    //this is the list of free nodes.
    //0 to 64bytes
    //8 bytes diff
    void ** list_1[8];
    int len_list_1[8];
    //64 to 2048bytess
    //64 bytes diff
    void ** list_2[31];
    int len_list_2[31];
    //2048 to 32kbytes
    //256 bytes diff
    void ** list_3[120];
    int len_list_3[120];
};

struct Thread_cache{
    struct Thread_cache * front;
    struct Thread_cache * back;

    pthread_t tid;

    struct thread_free_list list_;

};
//define Span object
struct Span{
    struct Span * front;
    struct Span * back;
    //if it's used to allocate small objects.
    unsigned long size;
    //if size == 0 -> it's not small objects
    void ** free_objects;
    int num_free_objects;
    int num_alloc_objects;
    
    //the start address of span.
    //num of pages
    void * start_addr;
    int num_pages;
};
//define central Free list
struct CentralFreeList{
    struct Span* non_empty;
    struct Span* empty;


    int num_re;
    void ** re_head_obj;
    void ** re_tail_obj;

};

struct Central_cache{
    //this is the list of free nodes.
    //0 to 64bytes
    //8 bytes diff
    struct CentralFreeList * list_1[8];
    //64 to 2048bytess
    //64 bytes diff
    struct CentralFreeList * list_2[31];
    //2048 to 32kbytes
    //256 bytes diff
    struct CentralFreeList * list_3[120]; 
};

struct Page_Heap{
    struct Span * pagemap[256];
    struct Radix_root * DB;
};

struct Meta{
    struct Central_cache central_cache;
    struct Thread_cache * first_thread_cache;
    struct Page_Heap central_page_heap;
};



struct Radix_root * Radix_tree_init(){
    //printf("Radix tree initalize\n");
    struct Radix_root * temp=NULL;
    temp = meta_addr;
    meta_addr+= sizeof(struct Radix_root);
    temp->height=6;
    temp->tot_cnt = 0;
    temp->root_node = meta_addr;
    meta_addr+=sizeof(struct Radix_node);
    temp->root_node->level=6;
    temp->root_node->cnt=0;
    for(int i=0;i<64;i++){
        temp->root_node->p_list[i]=NULL;
    }
    //add to meta data.
    meta_data->central_page_heap.DB = temp;
    return temp;
};

struct Span * search_tree(unsigned long long int in_pointer, struct Radix_root * DB){
    unsigned long long int page_num = 0;
    int index_list[6]={0,};
    struct Radix_node * iter;
    //첫번째 카이 가작 마지막의 것. 즉, 1이 들어오면, 100000이 저장되는 것.
    //쉰게 말해서 index + 1 = level'
    if(in_pointer%PAGE_SIZE!=0){
        //fprintf(stderr,"the input pointer is not multiple of PAGESIZE\n");
        page_num= in_pointer/PAGE_SIZE;
    }
    else{
        page_num = in_pointer/PAGE_SIZE;
    }
    //
    for(int i=0;i<6;i++){
        index_list[i]=page_num%64;
        page_num/=64;
    }
    iter = DB->root_node;
    for(int i=0;i<5;i++){
        if(iter->p_list[index_list[5-i]]==NULL){
            //printf("There isn't node in tree\n");
            return NULL;
        }
        iter = iter->p_list[index_list[5-i]];
    }
    if(iter->p_list[index_list[0]]==NULL){
        //printf("There isn't node in tree\n");
        return NULL;
    }
    return iter->p_list[index_list[0]];
}

void add_one_node(unsigned long long int in_pointer, struct Span * span_pointer,struct Radix_root * DB){
    //first get the opage number.
    unsigned long long int page_num = 0;
    int index_list[6]={0,};
    struct Radix_node * iter;
    struct Radix_node * new_node;
    //첫번째 칸이 가작 마지막의 것. 즉, 1이 들어오면, 100000이 저장되는 것.
    //쉰게 말해서 index + 1 = level'
    if(in_pointer%PAGE_SIZE!=0){
        fprintf(stdout,"TID : %x, pointer : %p\n", pthread_self(),in_pointer);
        fprintf(stdout,"the input pointer is not multiple of PAGESIZE\n");
        page_num= in_pointer/PAGE_SIZE;
    }
    else{
        page_num = in_pointer/PAGE_SIZE;
    }
    //
    for(int i=0;i<6;i++){
        index_list[i]=page_num%64;
        page_num/=64;
    }

    iter = DB->root_node;
    //이제 새롭게 노드를 추가하는 것임.
    //spin lock을 호출하는 함수에서 걸었을 거니까, 그냥 시작하면 된다.
    for(int i=5;i>0;i--){
        if(iter->p_list[index_list[i]]!=NULL){
            iter=iter->p_list[index_list[i]];
        }
        else{
            //make new node.
            if(meta_addr_end<=meta_addr + sizeof(struct Radix_node)){
                fprintf(stdout,"There isnt space for meta data\n");
                fflush(stdout);
                return NULL;
            }
            new_node = meta_addr;
            meta_addr+=sizeof(struct Radix_node);
            //initialize the new node.
            new_node->level=iter->level-1;
            new_node->cnt = 0;
            iter->cnt++;
            for(int j=0;j<64;j++){
                new_node->p_list[j]=NULL;
            }
            iter->p_list[index_list[i]] = new_node;
            iter = new_node;
        }
    }
    //이제 iter에는 leaf node가 담겨있음.
    if(iter->p_list[index_list[0]]!=NULL){
        //fprintf(stdout,"Remove previous spans and allocates new!\n");
        iter->p_list[index_list[0]]=span_pointer;
    }
    else{
        iter->p_list[index_list[0]]=span_pointer;
        iter->cnt++;
    } 
    DB->tot_cnt++;
    return;
}

void add_Span(struct Span * span_pointer, struct Radix_root * DB){
    void * start_addr = span_pointer->start_addr;
    int num_page = span_pointer->num_pages;
    //fprintf(stdout,"add span is called, addr : %p, num_obj : %d\n",start_addr,span_pointer->num_free_objects);
    //fprintf(stdout,"num_page : %d, real size: %llu, size : %u\n",span_pointer->num_pages,span_pointer->num_pages*PAGE_SIZE,span_pointer->size);
    for(int i=0;i<num_page;i++){
        add_one_node(start_addr, span_pointer, DB);
        start_addr+=PAGE_SIZE;
    }
    return;
}
///////////////////////////////////////////////////
//functions

//일단 임시로 여기다가 mmap풀수 있게 list를 쌓아두자. 
//void * mmap_init_list[1000] ={0,};
//int mmap_init_list_cnt = 0;

struct Span * merge_spans(struct Span * base, struct Span * add){
    fflush(stdout);
    //base 는 확실히 아무곳에도 안 이어져씽므.
    //add 는 pagemap에서 지워줘야해.
    struct Span * iter;
    if(add->num_pages<256){
        iter = meta_data->central_page_heap.pagemap[add->num_pages-1];
        if(iter == add){
            meta_data->central_page_heap.pagemap[add->num_pages-1]=iter->back;
            if(iter->back!=NULL){
                iter->back->front = NULL;
            }
        }
        else{
            add->front->back = add->back;
            if(add->back!=NULL){
                add->back->front = add->front;
            }
        }
    }
    else{
        iter = meta_data->central_page_heap.pagemap[255];
        if(iter == add){
            meta_data->central_page_heap.pagemap[255]=iter->back;
            if(iter->back!=NULL){
                iter->back->front = NULL;
            }
        }
        else{
            add->front->back = add->back;
            if(add->back!=NULL){
                add->back->front = add->front;
            }
        }
    }
    //이제 add를 탈출시켰으니, base에다가 두개를 묶자.
    if(base->start_addr>add->start_addr){
        base->start_addr = add->start_addr;
    }
    base->size+=add->size;
    base->num_pages+=add->num_pages;
    base->front = NULL;
    base->back = NULL;
    base->num_alloc_objects=0;
    base->num_free_objects=1;
    base->free_objects=base->start_addr;

    return base;
}

struct Span * alloc_pages(int num_page, size_t size){
    //Span을 return하는 함수이다.
    struct Span * return_addr = NULL;
    struct Span * remain_span = NULL;
    struct Span * iter = NULL;
    void * new_mapped_addr = NULL;
    int remain_page = 0;
    //int ret = pthread_spin_lock(&s_lock);
   
    //page heap의 span list중에서 return을 하는 부분
    if(num_page>255){
        //check in 255th slot.
        iter = meta_data->central_page_heap.pagemap[255];
        while(1){
            if(iter == NULL){
                break;
            }
            if(iter->num_pages>=num_page){
                //만약 더 크거나 같은 것을 찾았을 때.
                return_addr = iter;
                if(iter->front == NULL){
                    meta_data->central_page_heap.pagemap[255]=iter->back;
                }
                if(iter->back!=NULL){
                    iter->back->front = NULL;
                }
                //check whether the size is same or not.
                remain_page = iter->num_pages-num_page;
                if(remain_page==0){
                    break;
                }
                //if size is different, make a new Span.
                if(meta_addr_end<=meta_addr + sizeof(struct Span)){
                    fprintf(stdout,"There isnt space for meta data\n");
                    fflush(stdout);
                    return NULL;
                }
                remain_span = meta_addr;
                meta_addr+=sizeof(struct Span);  
                remain_span->size = remain_page * PAGE_SIZE;
                remain_span->start_addr = (return_addr->start_addr + num_page*PAGE_SIZE);
                remain_span->free_objects = remain_span->start_addr;
                remain_span->num_free_objects=1;
                remain_span->num_alloc_objects=0;
                remain_span->num_pages = remain_page;
                remain_span->front = NULL;
                if(remain_page<256){
                    remain_span->back = meta_data->central_page_heap.pagemap[remain_page-1];
                    if(remain_span->back!=NULL){
                        remain_span->back->front = remain_span;
                    }
                    meta_data->central_page_heap.pagemap[remain_page-1]=remain_span;
                }
                else{
                    //255번째 칸의 적절한 위치에 넣자.
                    iter = meta_data->central_page_heap.pagemap[255];
                    if(iter == NULL){
                        remain_span->front = NULL;
                        remain_span->back = NULL;
                        meta_data->central_page_heap.pagemap[255]=remain_span;
                    }
                    else if(iter->num_pages>remain_page){
                        remain_span->front = NULL;
                        remain_span->back = iter;
                        iter->front = remain_span;
                        meta_data->central_page_heap.pagemap[255]=remain_span;
                    }
                    else{
                        while(1){
                            if(iter->num_pages>remain_page){
                                iter->front->back = remain_span;
                                remain_span->front = iter->front;
                                iter->front = remain_span;
                                remain_span->back = iter;
                                break;
                            }
                            else if(iter->back == NULL){
                                iter->back = remain_span;
                                remain_span->front = iter;
                                remain_span->back = NULL;
                                break;
                            }
                            iter = iter->back;
                        }
                    }

                }
                //reduce original page map number.
                return_addr->num_pages = num_page;
                return_addr->size = num_page * PAGE_SIZE;

                //printf("original address : %p, size : %p\n", return_addr->start_addr, return_addr->size);
                //printf("splitted address : %p, size : %p\n", remain_span->start_addr, remain_span->size);

                //add to radix tree
                add_Span(remain_span,meta_data->central_page_heap.DB);
                add_Span(return_addr,meta_data->central_page_heap.DB);
                
            }
            iter = iter->back;
        }

    }
    else{
        for(int i=num_page-1;i<255;i++){
            if(meta_data->central_page_heap.pagemap[i]!=NULL){
                return_addr = meta_data->central_page_heap.pagemap[i];
                meta_data->central_page_heap.pagemap[i]=return_addr->back;
                if(meta_data->central_page_heap.pagemap[i]!=NULL){
                    meta_data->central_page_heap.pagemap[i]->front = NULL;
                }
                //check whether the size is same or not.
                remain_page = i+1-num_page;
                if(remain_page==0){
                    break;
                }
                //if size is different, make a new Span.
                if(meta_addr_end<=meta_addr + sizeof(struct Span)){
                    fprintf(stdout,"There isnt space for meta data\n");
                    fflush(stdout);
                    return NULL;
                }
                remain_span = meta_addr;
                meta_addr+=sizeof(struct Span);  
                remain_span->size = remain_page * PAGE_SIZE;
                remain_span->start_addr = (return_addr->start_addr + num_page*PAGE_SIZE);
                remain_span->free_objects = remain_span->start_addr;
                remain_span->num_free_objects=1;
                remain_span->num_alloc_objects=0;
                remain_span->num_pages = remain_page;
                remain_span->front = NULL;
                remain_span->back = meta_data->central_page_heap.pagemap[remain_page-1];
                if(remain_span->back!=NULL){
                    remain_span->back->front = remain_span;
                }
                meta_data->central_page_heap.pagemap[remain_page-1]=remain_span;
                //reduce original page map number.
                return_addr->num_pages = num_page;
                return_addr->size = num_page * PAGE_SIZE;

                //printf("original address : %p, size : %p\n", return_addr->start_addr, return_addr->size);
                //printf("splitted address : %p, size : %p\n", remain_span->start_addr, remain_span->size);

                //add to radix tree
                add_Span(remain_span,meta_data->central_page_heap.DB);
                add_Span(return_addr,meta_data->central_page_heap.DB);
                
                break;
            }
        }
    }
    //위에서 찾아봤는데, 없는 경우
    if(return_addr ==NULL){
        //fprintf(stdout,"New pages are called\n");
        //fflush(stdout);
        new_mapped_addr= mmap(NULL,num_page*PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        //mmap_init_list[mmap_init_list_cnt] = new_mapped_addr;
        //mmap_init_list_cnt++;
        
        if(meta_addr_end<=meta_addr + sizeof(struct Span)){
            fprintf(stdout,"There isnt space for meta data\n");
            fflush(stdout);
            return NULL;
        }   
        return_addr = meta_addr;
        meta_addr+=sizeof(struct Span);
        //이제 Span을 초기화해야 할 순서인데, front back을 이 함수에서 할것이 아니니까
        return_addr->free_objects = new_mapped_addr;
        return_addr->start_addr = new_mapped_addr;
        return_addr->num_pages = num_page;
        //add to radix tree
        add_Span(return_addr,meta_data->central_page_heap.DB);
        
    }
    //이제 주어진 size에 맞게 쪼개기를 시행하자.
    //근데 우리는 large allocation도 이걸로 처리를 하고 싶기 때문에, size를 먼저 check 할 것이다.
    return_addr->front = NULL;
    return_addr->back = NULL;
    return_addr->size = size;
    return_addr->num_pages = num_page;
    return_addr->num_alloc_objects = 0;

    //만약 이게 large allocation에서 호출된 것이면
    if(size > size_list_3[119]){
        //이경우는 추가적인 가공이 필요없는 상태임.
        return_addr->num_free_objects = 1;
    }
    //만약 이게 small allocation에서 호출된 것이면
    else{
        void * iter;
        return_addr->num_free_objects = ((int) num_page * PAGE_SIZE)/((int) size);
        return_addr->free_objects = return_addr->start_addr;
        iter = return_addr->free_objects;
        for(int i=1;i<return_addr->num_free_objects;i++){
            *((void**)iter) = iter + size;
            iter = iter + size;
        }
        *((void**)iter) = NULL;
        //fprintf(stdout,"------------result address : %p--------------\n",return_addr->start_addr);
        //fflush(stdout);
        
    }
    //add_Span(return_addr,meta_data->central_page_heap.DB);

    
    //ret = pthread_spin_unlock(&s_lock);
    return return_addr;
}

void * alloc_small_1(size_t size,struct Thread_cache * curr_tc){
    void ** result_addr = NULL;
    int cnt = 0;
    cnt = (size/8)-1;
    void ** iter = NULL;
    int ret;

    if(curr_tc->list_.len_list_1[cnt]==0){
        //thread cache에 들어있지 않다는 것./
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return -1;
        }
        //fprintf(stdout,"TID : %x, requesting memory from central free list\n",pthread_self());
        //first check the slots.
        if(meta_data->central_cache.list_1[cnt]->num_re > batch_size[0]){
            //when there are enough objects in slots.
            curr_tc->list_.list_1[cnt]=meta_data->central_cache.list_1[cnt]->re_head_obj;
            iter = curr_tc->list_.list_1[cnt];
            for(int i=1;i<batch_size[0];i++){
                iter = *iter;                                                                                                                                                                                                                                     
            }
            meta_data->central_cache.list_1[cnt]->re_head_obj = NULL;
            meta_data->central_cache.list_1[cnt]->re_tail_obj = NULL;
            meta_data->central_cache.list_1[cnt]->num_re -= batch_size[0];

            *iter = NULL;
        }
        //when there aren't enough objects in slots
        else{
            int req_remain = batch_size[0];
            struct Span * span_iter;
            struct Span * span_new;
            if(meta_data->central_cache.list_1[cnt]->num_re !=0){
                curr_tc->list_.list_1[cnt]=meta_data->central_cache.list_1[cnt]->re_head_obj;
                iter = meta_data->central_cache.list_1[cnt]->re_tail_obj;
                *(meta_data->central_cache.list_1[cnt]->re_tail_obj)=NULL;
                meta_data->central_cache.list_1[cnt]->re_tail_obj=NULL;
                meta_data->central_cache.list_1[cnt]->re_head_obj = NULL;
                req_remain -= meta_data->central_cache.list_1[cnt]->num_re;
                meta_data->central_cache.list_1[cnt]->num_re=0;
            }
            //Check whether there is enough space in Span.
            span_iter = meta_data->central_cache.list_1[cnt]->non_empty;
            for(int i=0;i<req_remain;i++){
                //non empty에 있는 Span을 다 썻을때. 
                //어차피 non_empty에는 새로 추가되는 친구밖에 없음.
                if(span_iter == NULL){
                    span_new = alloc_pages(100,size);
                    meta_data->central_cache.list_1[cnt]->non_empty = span_new;
                    span_iter = span_new;
                }
                if(iter!=NULL){
                    *iter = span_iter->free_objects;
                    iter = *iter;
                }
                else{
                    iter = span_iter->free_objects;
                    curr_tc->list_.list_1[cnt] = iter;
                }
                span_iter->free_objects=*(span_iter->free_objects);
                span_iter->num_alloc_objects +=1;
                span_iter->num_free_objects -=1;
                *iter = NULL;

                //만약 다 쓴 Span이면 empty로 들어가야지.
                if(span_iter->num_free_objects == 0){
                    //front의 back을 NULL로 바꿀 필요가 없는 이유는, 앞에꺼부터 가져와서 쓰기 때문이다.
                    if(meta_data->central_cache.list_1[cnt]->empty!=NULL){
                        meta_data->central_cache.list_1[cnt]->empty->front = span_iter;
                    }
                    span_new = span_iter -> back;
                    span_iter->back = meta_data->central_cache.list_1[cnt]->empty;
                    meta_data->central_cache.list_1[cnt]->empty=span_iter;
                    span_iter->front = NULL;
                    //change span_iter
                    if(span_new !=NULL){
                        span_new->front = NULL;
                    }
                    meta_data->central_cache.list_1[cnt]->non_empty=span_new;
                    span_iter=span_new;
                }
            }
            //fprintf(stderr,"allocation of %d objects to TC is done\n",batch_size[0]);

        }
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return -1;
        }
        curr_tc->list_.len_list_1[cnt]=batch_size[0];
    }


    curr_tc->list_.len_list_1[cnt]--;
    result_addr = curr_tc->list_.list_1[cnt];
    curr_tc->list_.list_1[cnt] = *result_addr;

    return result_addr;
}
void * alloc_small_2(size_t size, struct Thread_cache * curr_tc){
    void ** result_addr = NULL;
    int cnt = 0;
    cnt = (size/64)-2;
    void ** iter = NULL;
    int ret;

    if(curr_tc->list_.len_list_2[cnt]==0){
        //thread cache에 들어있지 않다는 것./
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return -1;
        }
        //fprintf(stdout,"TID : %x, requesting memory from central free list\n",pthread_self());
        //first check the slots.
        if(meta_data->central_cache.list_2[cnt]->num_re > batch_size[1]){
            //when there are enough objects in slots.
            curr_tc->list_.list_2[cnt]=meta_data->central_cache.list_2[cnt]->re_head_obj;
            iter = curr_tc->list_.list_2[cnt];
            for(int i=1;i<batch_size[1];i++){
                iter = *iter;                                                                                                                                                                                                                                     
            }
            meta_data->central_cache.list_2[cnt]->re_head_obj = NULL;
            meta_data->central_cache.list_2[cnt]->re_tail_obj = NULL;
            meta_data->central_cache.list_2[cnt]->num_re -= batch_size[1];

            *iter = NULL;
        }
        //when there aren't enough objects in slots
        else{
            int req_remain = batch_size[1];
            struct Span * span_iter = NULL;
            struct Span * span_new = NULL;
            if(meta_data->central_cache.list_2[cnt]->num_re !=0){
                curr_tc->list_.list_2[cnt]=meta_data->central_cache.list_2[cnt]->re_head_obj;
                iter = meta_data->central_cache.list_2[cnt]->re_tail_obj;
                *(meta_data->central_cache.list_2[cnt]->re_tail_obj)=NULL;
                meta_data->central_cache.list_2[cnt]->re_tail_obj=NULL;
                meta_data->central_cache.list_2[cnt]->re_head_obj = NULL;
                req_remain -= meta_data->central_cache.list_2[cnt]->num_re;
                meta_data->central_cache.list_2[cnt]->num_re=0;
            }
            //Check whether there is enough space in Span.
            span_iter = meta_data->central_cache.list_2[cnt]->non_empty;
            
            for(int i=0;i<req_remain;i++){
                //non empty에 있는 Span을 다 썻을때. 
                //어차피 non_empty에는 새로 추가되는 친구밖에 없음.
                if(span_iter == NULL){
                    //fprintf(stdout,"why???????????????????\n");
                    span_new = alloc_pages(100,size);
                    meta_data->central_cache.list_2[cnt]->non_empty = span_new;
                    span_iter = span_new;
                }
                //
                if(iter!=NULL){
                    *iter = span_iter->free_objects;
                    iter = *iter;
                }
                else{
                    iter = span_iter->free_objects;
                    curr_tc->list_.list_2[cnt] = iter;
                }
                span_iter->free_objects=*(span_iter->free_objects);
                span_iter->num_alloc_objects ++;
                span_iter->num_free_objects --;
                //printf("remain : %d\n",span_iter->num_free_objects);
                *iter = NULL;

                //만약 다 쓴 Span이면 empty로 들어가야지.
                if(span_iter->num_free_objects == 0){
                    //fprintf(stderr,"kokokokokokok moving old one\n");
                    //front의 back을 NULL로 바꿀 필요가 없는 이유는, 앞에꺼부터 가져와서 쓰기 때문이다.
                    if(meta_data->central_cache.list_2[cnt]->empty!=NULL){
                        meta_data->central_cache.list_2[cnt]->empty->front = span_iter;
                    }
                    span_new = span_iter -> back;
                    span_iter->back = meta_data->central_cache.list_2[cnt]->empty;
                    meta_data->central_cache.list_2[cnt]->empty=span_iter;
                    span_iter->front = NULL;
                    //change span_iter
                    if(span_new !=NULL){
                        span_new->front = NULL;
                    }
                    meta_data->central_cache.list_2[cnt]->non_empty=span_new;
                    span_iter=span_new;
                    //fprintf(stderr,"new:%p\n",span_iter);
                }
            }
            //fprintf(stderr,"allocation of %d objects to TC is done\n",batch_size[0]);
        }
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return -1;
        }
        curr_tc->list_.len_list_2[cnt]=batch_size[1];
    }
    


    curr_tc->list_.len_list_2[cnt]--;
    result_addr = curr_tc->list_.list_2[cnt];
    curr_tc->list_.list_2[cnt] = *result_addr;

    return result_addr;
}
void * alloc_small_3(size_t size, struct Thread_cache * curr_tc){
    void ** result_addr = NULL;
    int cnt = 0;
    cnt = (size/256)-9;
    void ** iter = NULL;
    int ret;

    if(curr_tc->list_.len_list_3[cnt]==0){
        //thread cache에 들어있지 않다는 것./
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return -1;
        }
        //fprintf(stdout,"TID : %x, requesting memory from central free list\n",pthread_self());
        //first check the slots.
        if(meta_data->central_cache.list_3[cnt]->num_re > batch_size[2]){
            //when there are enough objects in slots.
            curr_tc->list_.list_3[cnt]=meta_data->central_cache.list_3[cnt]->re_head_obj;
            iter = curr_tc->list_.list_3[cnt];
            for(int i=1;i<batch_size[2];i++){
                iter = *iter;                                                                                                                                                                                                                                     
            }
            meta_data->central_cache.list_3[cnt]->re_head_obj = NULL;
            meta_data->central_cache.list_3[cnt]->re_tail_obj = NULL;
            meta_data->central_cache.list_3[cnt]->num_re -= batch_size[2];

            *iter = NULL;
        }
        //when there aren't enough objects in slots
        else{
            int req_remain = batch_size[2];
            struct Span * span_iter;
            struct Span * span_new;
            if(meta_data->central_cache.list_3[cnt]->num_re !=0){
                curr_tc->list_.list_3[cnt]=meta_data->central_cache.list_3[cnt]->re_head_obj;
                iter = meta_data->central_cache.list_3[cnt]->re_tail_obj;
                *(meta_data->central_cache.list_3[cnt]->re_tail_obj)=NULL;
                meta_data->central_cache.list_3[cnt]->re_tail_obj=NULL;
                meta_data->central_cache.list_3[cnt]->re_head_obj = NULL;
                req_remain -= meta_data->central_cache.list_3[cnt]->num_re;
                meta_data->central_cache.list_3[cnt]->num_re=0;
            }
            //Check whether there is enough space in Span.
            span_iter = meta_data->central_cache.list_3[cnt]->non_empty;
            for(int i=0;i<req_remain;i++){
                //non empty에 있는 Span을 다 썻을때. 
                //어차피 non_empty에는 새로 추가되는 친구밖에 없음.
                if(span_iter == NULL){
                    span_new = alloc_pages(100,size);
                    meta_data->central_cache.list_3[cnt]->non_empty = span_new;
                    span_iter = span_new;
                }
                if(iter!=NULL){
                    *iter = span_iter->free_objects;
                    iter = *iter;
                }
                else{
                    iter = span_iter->free_objects;
                    curr_tc->list_.list_3[cnt] = iter;
                }
                span_iter->free_objects=*(span_iter->free_objects);
                span_iter->num_alloc_objects +=1;
                span_iter->num_free_objects -=1;
                *iter = NULL;

                //만약 다 쓴 Span이면 empty로 들어가야지.
                if(span_iter->num_free_objects == 0){
                    //front의 back을 NULL로 바꿀 필요가 없는 이유는, 앞에꺼부터 가져와서 쓰기 때문이다.
                    if(meta_data->central_cache.list_3[cnt]->empty!=NULL){
                        meta_data->central_cache.list_3[cnt]->empty->front = span_iter;
                    }
                    span_new = span_iter -> back;
                    span_iter->back = meta_data->central_cache.list_3[cnt]->empty;
                    meta_data->central_cache.list_3[cnt]->empty=span_iter;
                    span_iter->front = NULL;
                    //change span_iter
                    if(span_new !=NULL){
                        span_new->front = NULL;
                    }
                    meta_data->central_cache.list_3[cnt]->non_empty=span_new;
                    span_iter=span_new;
                }
            }
            //fprintf(stderr,"allocation of %d objects to TC is done\n",batch_size[2]);

        }
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return -1;
        }
        curr_tc->list_.len_list_3[cnt]=batch_size[2];
    }
    


    curr_tc->list_.len_list_3[cnt]--;
    result_addr = curr_tc->list_.list_3[cnt];
    curr_tc->list_.list_3[cnt] = *result_addr;

    return result_addr;
}

void tc_garbage_collect(struct Thread_cache * curr_tc, int list_n, int i){
    int ret;
    if(list_n == 1){
        if(i>=8){
            fprintf(stdout,"wrong input index.\n");
            return;
        }
        if(curr_tc->list_.len_list_1[i]>batch_size[0]/2){
            ret = pthread_spin_lock(&s_lock);
            printf("list_%d[%d] GC\n",list_n,i);
            for(int k=0; k<curr_tc->list_.len_list_1[i]/2; k++){
                if(meta_data->central_cache.list_1[i]->re_tail_obj == NULL){
                    meta_data->central_cache.list_1[i]->re_tail_obj = curr_tc->list_.list_1[i];
                    meta_data->central_cache.list_1[i]->re_head_obj = curr_tc->list_.list_1[i];
                }
                else{
                    *(meta_data->central_cache.list_1[i]->re_tail_obj)=curr_tc->list_.list_1[i];
                    meta_data->central_cache.list_1[i]->re_tail_obj=curr_tc->list_.list_1[i];
                }
                curr_tc->list_.list_1[i] = *(curr_tc->list_.list_1[i]);
                *(meta_data->central_cache.list_1[i]->re_tail_obj)=NULL;
                meta_data->central_cache.list_1[i]->num_re++;
            }
            ret = pthread_spin_unlock(&s_lock);
        }
    }
    else if(list_n==2){
        if(i>=31){
            fprintf(stdout,"wrong input index.\n");
            return;
        }
        if(curr_tc->list_.len_list_2[i]>batch_size[1]/2){
            ret = pthread_spin_lock(&s_lock);
            printf("list_%d[%d] GC\n",list_n,i);
            for(int k=0; k<curr_tc->list_.len_list_2[i]/2; k++){
                if(meta_data->central_cache.list_2[i]->re_tail_obj == NULL){
                    meta_data->central_cache.list_2[i]->re_tail_obj = curr_tc->list_.list_2[i];
                    meta_data->central_cache.list_2[i]->re_head_obj = curr_tc->list_.list_2[i];
                }
                else{
                    *(meta_data->central_cache.list_2[i]->re_tail_obj)=curr_tc->list_.list_2[i];
                    meta_data->central_cache.list_2[i]->re_tail_obj=curr_tc->list_.list_2[i];
                }
                curr_tc->list_.list_2[i] = *(curr_tc->list_.list_2[i]);
                *(meta_data->central_cache.list_2[i]->re_tail_obj)=NULL;
                meta_data->central_cache.list_2[i]->num_re++;
            }
            ret = pthread_spin_unlock(&s_lock);
        }
    }
    else if(list_n==3){
        if(i>=120){
            fprintf(stdout,"wrong input index.\n");
            return;
        }
        if(curr_tc->list_.len_list_3[i]>batch_size[2]/2){
            ret = pthread_spin_lock(&s_lock);
            printf("list_%d[%d] GC\n",list_n,i);
            for(int k=0; k<curr_tc->list_.len_list_3[i]/2; k++){
                if(meta_data->central_cache.list_3[i]->re_tail_obj == NULL){
                    meta_data->central_cache.list_3[i]->re_tail_obj = curr_tc->list_.list_3[i];
                    meta_data->central_cache.list_3[i]->re_head_obj = curr_tc->list_.list_3[i];
                }
                else{
                    *(meta_data->central_cache.list_3[i]->re_tail_obj)=curr_tc->list_.list_3[i];
                    meta_data->central_cache.list_3[i]->re_tail_obj=curr_tc->list_.list_3[i];
                }
                curr_tc->list_.list_3[i] = *(curr_tc->list_.list_3[i]);
                *(meta_data->central_cache.list_3[i]->re_tail_obj)=NULL;
                meta_data->central_cache.list_3[i]->num_re++;
            }
            ret = pthread_spin_unlock(&s_lock);
        }
    }
    else{
        fprintf(stderr,"wrong input index.\n");
        return  ;
    }
    printf("GC is done\n");
    return;
}

void * tc_central_init(){
    //initialize the spin lock.
    //int ret = pthread_spin_init(&s_lock,NULL);
    int ret;
    if((ret = pthread_spin_init(&s_lock,NULL))!=0)
        return -1;
    if((ret = pthread_spin_lock(&s_lock))!=0)
        return -1;
    //meta data를 담아둘 meta_addr를 정의한다.
    meta_addr = mmap(NULL,(size_t) (20000*PAGE_SIZE), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    meta_addr_init = meta_addr;
    meta_addr_end = meta_addr_init + 20000*PAGE_SIZE;

    //allocate the meta_data
    meta_data = meta_addr;
    meta_addr += sizeof(struct Meta);

    //initalize the central cache
    for(int i=0;i<num_ind[0];i++){
        //initialize the central cache, by setting span as NULL
        meta_data->central_cache.list_1[i] = meta_addr;
        meta_addr += sizeof(struct CentralFreeList);
        meta_data->central_cache.list_1[i]->empty=NULL;
        meta_data->central_cache.list_1[i]->non_empty=NULL;
        meta_data->central_cache.list_1[i]->non_empty=0;
        meta_data->central_cache.list_1[i]->re_head_obj = NULL;
        meta_data->central_cache.list_1[i]->re_tail_obj = NULL;
        meta_data->central_cache.list_1[i]->num_re = 0;

    }
    for(int i=0;i<num_ind[1];i++){
        //initialize the central cache, by setting span as NULL
        meta_data->central_cache.list_2[i] = meta_addr;
        meta_addr += sizeof(struct CentralFreeList);
        meta_data->central_cache.list_2[i]->empty=NULL;
        meta_data->central_cache.list_2[i]->non_empty=NULL;
        meta_data->central_cache.list_2[i]->non_empty=0;
        meta_data->central_cache.list_2[i]->re_head_obj = NULL;
        meta_data->central_cache.list_2[i]->re_tail_obj = NULL;
        meta_data->central_cache.list_2[i]->num_re = 0;
    }
    for(int i=0;i<num_ind[2];i++){
        //initialize the central cache, by setting span as NULL
        meta_data->central_cache.list_3[i] = meta_addr;
        meta_addr += sizeof(struct CentralFreeList);
        meta_data->central_cache.list_3[i]->empty=NULL;
        meta_data->central_cache.list_3[i]->non_empty=NULL;
        meta_data->central_cache.list_3[i]->non_empty=0;
        meta_data->central_cache.list_3[i]->re_head_obj = NULL;
        meta_data->central_cache.list_3[i]->re_tail_obj = NULL;
        meta_data->central_cache.list_3[i]->num_re = 0;
    }

    //tc_central_init()
    meta_data->first_thread_cache=NULL;

    //central page heap을 초기화 하는 곳.
    for(int i=0;i<256;i++){
        meta_data->central_page_heap.pagemap[i]=NULL;
    }
    //radix tree init
    Radix_tree_init();
    //ret = pthread_spin_unlock(&s_lock);

    if((ret = pthread_spin_unlock(&s_lock))!=0)
        return -1;

    return (void*) &(meta_data->central_cache);
}

void * tc_thread_init(){
    //i SHOULD check whether there is enough meta data addr.
    //printf("thread_init\n");
    //fflush(stdout);
    int ret;
    /*
    if(meta_addr == NULL){
        if(tc_central_init()==-1){
            fprintf(stderr,"fail to init central heap\n");
            return -1;
        }
        fprintf(stderr, "central init is done\n");
    }
    */
    
    if(ret = pthread_spin_lock(&s_lock)!=0){
        fprintf(stderr,"fail to spin lock\n");
        return -1;
    }
    //fprintf(stderr, "start thread init\n");
    
    

    struct Thread_cache * new_thread_cache = NULL;
    //int ret = pthread_spin_lock(&s_lock);
    if(meta_addr_end<=meta_addr + sizeof(struct Thread_cache)){

        fprintf(stdout,"There isnt space for meta data\n");
        fflush(stdout);

        return NULL;
    }

    //I'll add new node at the end.
    new_thread_cache = meta_addr;
    new_thread_cache->back = NULL;
    meta_addr+=sizeof(struct Thread_cache);
    
    if(meta_data->first_thread_cache==NULL){
        meta_data->first_thread_cache=new_thread_cache;
        new_thread_cache->front = NULL;
    }
    else{
        struct Thread_cache * temp = meta_data->first_thread_cache;
        while(1){
            if(temp->back == NULL){
                temp->back = new_thread_cache;
                new_thread_cache->front = temp;
                break;
            }
            temp = temp->back;
        }
    }
    

    if((ret = pthread_spin_unlock(&s_lock))!=0){
        fprintf(stderr,"fail to spin unlock\n");
        return -1;
        
    }

    //Now I connected the Thread_cache, so initialize the free list.

    new_thread_cache->tid = pthread_self();
    
    for(int i=0;i<num_ind[0];i++){
        new_thread_cache->list_.list_1[i] = NULL;
        new_thread_cache->list_.len_list_1[i] = 0;
    }
    for(int i=0;i<num_ind[1];i++){
        new_thread_cache->list_.list_2[i] = NULL;
        new_thread_cache->list_.len_list_2[i] = 0;
    }
    for(int i=0;i<num_ind[2];i++){
        new_thread_cache->list_.list_3[i] = NULL;
        new_thread_cache->list_.len_list_3[i] = 0;
    }
    

    //printf("Added thread cache, TID : %x \n", new_thread_cache->tid);

    return new_thread_cache;
}

void * tc_malloc(size_t size){
    
    void * result_addr = NULL;

    int ret;
    pthread_t curr_tid = pthread_self();
    struct Thread_cache * curr_tc = NULL;
    //fprintf(stderr, "tc_malloc called, tid : %x, required size : %d\n", curr_tid,size);
    /*
    if(meta_addr == NULL){
        ret = pthread_spin_init(&s_lock,NULL);
        ret = pthread_spin_lock(&s_lock);
        tc_central_init();
        curr_tc=tc_thread_init();
        ret = pthread_spin_unlock(&s_lock);
    }
    */

    //check whether these is thread cache.
    if(ret = pthread_spin_lock(&s_lock)!=0){
        fprintf(stderr,"fail to spin lock\n");
        return -1;
    }
    struct Thread_cache * iter = meta_data->first_thread_cache;
    char req_add = 1;
    int cnt=0;



    while(1){
        cnt++;
        if(iter->tid == curr_tid){
            //fprintf(stderr,"there is thread_cache already, %dth\n",cnt);
            curr_tc = iter;
            req_add=0;
        }
        if(iter->back!=NULL)
            iter = iter->back;
        else
            break;
    }
    if((ret = pthread_spin_unlock(&s_lock))!=0){
        fprintf(stderr,"fail to spin unlock\n");
        return -1;
        
    }

    if(req_add==1){
        if((curr_tc=tc_thread_init())==-1)
            return -1;
    }

    //printf("Num_thread_cache before adding new : %d\n",cnt);
    //printf("addr chng curr: %p, max: %p\n", meta_addr, meta_addr_end);

    //이제부턴 curr_tc에서 호출하는 과정을 만들어야 하는데. 일단 테스트 부터 하자.
    //첫번째, size를 계산한다/
    if(size > size_list_3[119]){
        int num_page = 0;
        struct Span * temp_span; 
        if(size%PAGE_SIZE==0){
            num_page = size/PAGE_SIZE;
        }
        else{
            num_page = size/PAGE_SIZE;
        }
        
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return -1;
        }
        
        temp_span = alloc_pages(num_page,size);
        result_addr = temp_span->free_objects;
        temp_span->num_alloc_objects++;
        temp_span->num_free_objects--;
        //large memory block
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return -1;    
        }
    }
    else{
        if(size<size_list_1[7]){
            //첫번째 array에서 찾아야 하는 경우. 
            if(size%8!=0){
                size=size-size%8+8;
            }
            if((result_addr = alloc_small_1(size,curr_tc))==-1){
                fprintf(stderr,"fail to alloc small 1\n");
                return -1;
            }
        }
        else if(size<size_list_2[30]){
            if(size%64!=0){
                size=size-size%64+64;
            }
            if((result_addr = alloc_small_2(size,curr_tc))==-1){
                fprintf(stderr,"fail to alloc small 1\n");
                return -1;
            }
        }
        else{
            if(size%256!=0){
                size=size-size%256+256;
            }
            if((result_addr = alloc_small_3(size,curr_tc))==-1){
                fprintf(stderr,"fail to alloc small 1\n");
                return -1;
            }
        }  
    }

    return result_addr;

}

void tc_free(void *ptr){


    //first check the size of the ptr.
    struct Span * cur_Span;
    struct Span * iter;
    int ret;
    //
    //어디선가 Span을 찾아오기
    //
    if(ret = pthread_spin_lock(&s_lock)!=0){
        fprintf(stderr,"fail to spin lock\n");
        return;
    }
    cur_Span=search_tree(ptr, meta_data->central_page_heap.DB);
    
    //if(cur_Span!=NULL){
    //    printf("I found the object to free\n");
    //}
    size_t cur_size = cur_Span->size;
    if((ret = pthread_spin_unlock(&s_lock))!=0){
        fprintf(stderr,"fail to spin unlock\n");
        return;
    }
    int list_n=0;
    int ind = 0;
    
    struct Thread_cache * curr_tc;
    pthread_t curr_tid = pthread_self();

    if(cur_size>size_list_3[119]){
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return;
        }
        cur_Span->num_alloc_objects--;
        cur_Span->num_free_objects++;
        //printf("tid:%x | freeing %p\n",curr_tid, ptr);
        
        //large size freeing.

        //check radix tree and merge the size_lists.
        //finally return to heap_page manager    
        //일단 주변것들을 확인하고 free하러 들어가야 한다.
        //즉 radix table을 구현해야 하는 것.
        //printf("curr addr : %p, back addr: %p\n",cur_Span->start_addr, cur_Span->start_addr+cur_Span->num_pages*PAGE_SIZE);
        struct Span * front = search_tree((cur_Span->start_addr-PAGE_SIZE),meta_data->central_page_heap.DB);
        struct Span * back = search_tree((cur_Span->start_addr+cur_Span->num_pages*PAGE_SIZE),meta_data->central_page_heap.DB);
        if(front!=NULL){
            if(front->num_alloc_objects==0 && front->num_free_objects==1){
                //printf("merge with front\n");
                //merrge해야함.
                //Span을 삭제하는 것은 메모리 낭비이긴 한데, 여튼 안쓰는게 생기는 것임.
                cur_Span=merge_spans(cur_Span,front);
                add_Span(cur_Span,meta_data->central_page_heap.DB);
            }
        }
        if(back!=NULL){
            if(back->num_alloc_objects==0&& back->num_free_objects==1){
                //printf("merge with back\n");
                cur_Span=merge_spans(cur_Span,back);
                add_Span(cur_Span,meta_data->central_page_heap.DB);
            }
        }
        

        if(cur_Span->num_pages<256){
            cur_Span->front = NULL;
            cur_Span->back = meta_data->central_page_heap.pagemap[cur_Span->num_pages-1];
            if(cur_Span->back!=NULL){
                cur_Span->back->front = cur_Span;
            }
            meta_data->central_page_heap.pagemap[cur_Span->num_pages-1]=cur_Span;
        }
        else{
            //255번째 칸의 적절한 위치에 넣자.
            iter = meta_data->central_page_heap.pagemap[255];
            if(iter == NULL){
                cur_Span->front = NULL;
                cur_Span->back = NULL;
                meta_data->central_page_heap.pagemap[255]=cur_Span;
            }
            else if(iter->num_pages>cur_Span->num_pages){
                cur_Span->front = NULL;
                cur_Span->back = iter;
                iter->front = cur_Span;
                meta_data->central_page_heap.pagemap[255]=cur_Span;
            }
            else{
                while(1){
                    if(iter->num_pages>cur_Span->num_pages){
                        iter->front->back = cur_Span;
                        cur_Span->front = iter->front;
                        iter->front = cur_Span;
                        cur_Span->back = iter;
                        break;
                    }
                    else if(iter->back == NULL){
                        iter->back = cur_Span;
                        cur_Span->front = iter;
                        cur_Span->back = NULL;
                        break;
                    }
                    iter = iter->back;
                }
            }
        }
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return;
        }   
    }
    else{
        //small size freeing.
        //first check the thread id. 
        if(ret = pthread_spin_lock(&s_lock)!=0){
            fprintf(stderr,"fail to spin lock\n");
            return;
        }
        curr_tc= meta_data->first_thread_cache;
        while(1){
            if(curr_tc == NULL){
                fprintf(stdout,"There is no thread cache to return the memory\n");
                fprintf(stdout,"failed to free %d, tid : %x", cur_size, curr_tid);
                return;
            }
            if(curr_tc->tid == curr_tid){
                break;
            }
            curr_tc = curr_tc->back;
        }
        if((ret = pthread_spin_unlock(&s_lock))!=0){
            fprintf(stderr,"fail to spin unlock\n");
            return;
        }
        //put them in the thread caches
        if(cur_size<size_list_1[7]){
            list_n=1;
            ind = cur_size/8-1;
            *((void **) ptr) = curr_tc->list_.list_1[ind];
            curr_tc->list_.list_1[ind] = ptr;
            curr_tc->list_.len_list_1[ind]++;
        }
        else if(cur_size<size_list_2[30]){
            list_n=2;
            ind = cur_size/64-2;
            *((void **) ptr) = curr_tc->list_.list_2[ind];
            curr_tc->list_.list_2[ind] = ptr;
            curr_tc->list_.len_list_2[ind]++;
        }
        else{
            list_n=3;
            ind = cur_size/256-9;
            *((void **) ptr) = curr_tc->list_.list_3[ind];
            curr_tc->list_.list_3[ind] = ptr;
            curr_tc->list_.len_list_3[ind]++;
        }
        //tc_garbage_collect(curr_tc,list_n,ind);
    }
    
    return;
}




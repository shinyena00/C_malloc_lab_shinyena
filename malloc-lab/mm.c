/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 10",
    /* First member's full name */
    "Shin Ye Na",
    /* First member's email address */
    "syn77@pusan.ac.kr",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12) // 페이지 단위 할당

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p)) // int로 읽기 
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define PREV_VAL(p)  (*(unsigned int *)(p)) // 똑같은 건데 가독성을 위해서 추가함 
#define NEXT_VAL(p)  (*(unsigned int *)((char *)(p)+ WSIZE))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXTP(bp) ((char *)(bp) + WSIZE) // bp 블록 안의 succ
#define PREVP(bp) ((char *)(bp))

#define offtoptr(bp, off) ((off) ? (heap_base) + (off) : NULL) // heap listp 에서 offset을 더하는 방식 채택 
#define ptrtooff(bp) ((bp) != NULL ? (unsigned int)((char *)(bp) - heap_base) : 0)

#define NEXT_FREE(bp) (offtoptr((bp), NEXT_VAL(bp))) // 명시적 가용 리스트 이동 하는 거 , 진짜 이동 
#define PREV_FREE(bp) (offtoptr((bp), PREV_VAL(bp)))

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))


/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 정렬의 배수로 올림 (~0x7로 지움 구현)
#define ADJUST_SIZE(size) (((size) <= DSIZE) ? 2 * DSIZE : ALIGN((size) + DSIZE))


static void *extend_heap(size_t words); // 함수 프로토타입 선언 
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void disconnect(void *bp); //malloc에 사용 
static void insert(void *bp); // free에 사용 
int expand_block(void *bp, size_t size); //realloc에 사용 
static size_t up_pow(size_t size); // 2의 거듭제곱만큼 할당 

static char *heap_listp; // 전역변수로 포인터 선언 
static char* head; // freelist를 위한 전역변수 head는 그냥 포인터로 관리하면 된다. 
static char* heap_base; 


/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{

    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }

    PUT(heap_listp, 0); // null 문자가 가르키는 곳 
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1)); //  Epilogue header 
    heap_base = heap_listp; // null 이 0과 같아서 heap_base를 heap_listp로 초기화해야한다. 
    heap_listp += 4 *WSIZE; // 첫 청크부터 시작하는 작은 최적화(처음에는 heap의 끝을 가르킴 )
    head = NULL; // free list 초기화 

    if(extend_heap(CHUNKSIZE / WSIZE) == NULL){ // heap 공간 확보 
        return -1;
    }



    return 0; // 정상종료

    
}

static void *extend_heap(size_t words)
{
    size_t size;
    char *bp;

    size = (words%2) ? (words+1) * WSIZE : words * WSIZE;
    
    if((bp = mem_sbrk(size)) == (void *)-1){
        return NULL;
    }

    PUT((char *)bp - WSIZE, PACK(size, 0)); // new chunk header
    PUT(FTRP(bp), PACK(size, 0));     // new chunk footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); // epilogue

    return coalesce(bp);

}

static size_t up_pow(size_t size){
    if(size <= 64 || size >= 1024){
        return size;
    }

    int top = 63 - __builtin_clzl(size - 1); // 128 일 떄도 128로 올려버리면 안 되니까 -1하고 계산 
    return (size_t )1 << (top + 1);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    void * bp;
    size_t extend;

    if(size == 0){
        return NULL;
    }

    size = ADJUST_SIZE(up_pow(size)); //일단 malloc에만 적용 

    if((bp = find_fit(size)) != NULL){
        place(bp, size);
        disconnect(bp);
        return bp;
    }
    else{
        extend = MAX(size, CHUNKSIZE);
        if((bp = extend_heap(extend / WSIZE)) == NULL){ // 메모리 없으면 extend 
            return NULL;
        }

        place(bp, size);
        disconnect(bp);
        return bp;
    }

}




static void *find_fit(size_t asize)
{

    char * current = head;
    char * best =  NULL; // 이건 head가 asize에 안 맞을 수도 있으니까 처음엔 NULL로 설정해야함 
    char * top = NULL;

    for(; current != NULL; current = NEXT_FREE(current)){
        if(GET_SIZE(HDRP(NEXT_BLKP(current))) == 0){ //top에 붙은 청크면 후순위로 미룸 
            if(GET_SIZE(HDRP(current)) >= asize){
                top = current;
            }
            continue;
        }
        if(GET_SIZE(HDRP(current)) == asize){
            return current;
        }


        if(GET_SIZE(HDRP(current)) > asize){
            if(!best){
                best = current;
            }
            else if(GET_SIZE(HDRP(best)) > GET_SIZE(HDRP(current))){
                best = current;
            }
        }
    }

    if(!best && top){
        return top;
    }
    return best;
}

static void disconnect(void *bp){


    if(PREV_VAL(bp) != 0){
        PUT(NEXTP(PREV_FREE(bp)), NEXT_VAL(bp));
    }
    else{
        head = NEXT_FREE(bp);
    }

    if(NEXT_VAL(bp) != 0){
        PUT(PREVP(NEXT_FREE(bp)), PREV_VAL(bp));
    }
}
static void insert(void *bp){
    PUT(PREVP(bp), 0); // 연결을 size_t offset으로 관리하기 때문에 null이 아닌 0으로 취급해줘야한다 
    PUT(NEXTP(bp), ptrtooff(head));
    if(head != NULL){
        PUT(PREVP(head), ptrtooff(bp)); // head가 null이 아닐 때로 뒤도 앞을 가리키게 
    }
    head = bp;
}

static void place(void *bp, size_t size){


    size_t total = GET_SIZE(HDRP(bp));

    if(total - size < 3 * DSIZE){
        PUT(HDRP(bp), PACK(total, 1));
        PUT(FTRP(bp), PACK(total, 1));
        return; // 분할 햇다고 생각햇을 때 total-size 가 12바이트보다 작으면 그냥 return -> 이 조건에서 total == size인 경우도 잡아짐 
    }

    PUT(HDRP(bp), PACK(size, 1));
    PUT(FTRP(bp), PACK(size, 1));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(total-size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(total-size, 0));

    insert(NEXT_BLKP(bp));



}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    if(bp == NULL){ // NULL이 들어오는 경우 안 터지게 방지
        return;
    }
    size_t size = GET_SIZE(HDRP(bp)); // 원래 ptr이었는데 bp로 통일 

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);

}

//병합 함수 
static void *coalesce(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    if(prev_alloc && next_alloc == 1){
        insert(bp);
        return bp;
    }

    else if(prev_alloc && !next_alloc){ // 뒤가 free일 때

        disconnect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0)); // 위에서 header size를 바꿔서 NEXT_BLKP -> FTRP로 접근해야한다
        insert(bp);


        return bp;
    }

    else if(!prev_alloc && next_alloc){ // 앞 청크가 free 일 때 

        disconnect(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

    }

    else{

        disconnect(PREV_BLKP(bp));
        disconnect(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 여기는 현재 header size를 안 건드려서 next_blkp 접근 가능 

    }

    insert(PREV_BLKP(bp));
    return PREV_BLKP(bp);
}


int expand_block(void *bp, size_t size){
    char *next = HDRP(NEXT_BLKP(bp));
    size_t asize = GET_SIZE(HDRP(bp));

    if(GET_ALLOC(next) == 0 && GET_SIZE(next) + asize >= size){
        disconnect(NEXT_BLKP(bp));
        asize += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        return 1;
    }
    else if(GET_SIZE(next) == 0){ // 뒤가 epiloque면 heap을 늘려서 돌려주기 
        size_t extend = MAX(size - asize, CHUNKSIZE);
        if((extend_heap(extend / WSIZE)) == NULL){ // 메모리 없으면 extend 
            return 0;
        }

        expand_block(bp, size); // 앞이랑 합치기
        place(bp, size); // 분할
        return 1;
    }

    return 0;
}
/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size)
{
    if(size == 0){
        mm_free(bp); // size를 0으로 realloc하면 free와 같이 움직여야함 
        return NULL;
    }


    size_t asize = ADJUST_SIZE(size);

    if(bp != NULL){
        if(asize <= GET_SIZE(HDRP(bp))){ // 축소하는 경우 place로 분할까지 대체
            place(bp, asize);
            return bp;
        }

        if(expand_block(bp, asize)){
            return bp;
        }
    }


    void *oldbp = bp;
    void *newbp;
    size_t copySize;

    newbp = mm_malloc(size);

    if (newbp == NULL)
        return NULL;

    if(bp == NULL){
        return newbp; // bp를 NULL로 넣은 경우엔 일반 malloc과 같음 
    }

    copySize = GET_SIZE(HDRP(oldbp)) - DSIZE;
    if (size < copySize)
        copySize = size;

    memcpy(newbp, oldbp, copySize);
    mm_free(oldbp);

    return newbp;
}

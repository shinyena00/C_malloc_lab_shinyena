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

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)bp - DSIZE))

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 정렬의 배수로 올림 (~0x7로 지움 구현)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // size_t를 올림 -> 여기서는 8바이트 즉, 푸터와 헤더 8바이트를 더해서 배수 구함

static void *extend_heap(size_t words); // 함수 프로토타입 선언 
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static char *heap_listp; // 전역변수로 포인터 선언 
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{

    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1)); //  Epilogue header 
    heap_listp += 2 *WSIZE; // heaplist를 prologue header 뒤로 옮김 

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
    PUT(FTRP(bp), PACK(size, 0));     // newe chunk footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); // epilogue

    return coalesce(bp);

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

    if(size <= DSIZE){
        size = 2 * DSIZE;
    }
    else{
        size = ALIGN(size + SIZE_T_SIZE); // 올림 
    }

    if((bp = find_fit(size)) != NULL){
        place(bp, size);
        return bp;
    }
    else{
        extend = MAX(size, CHUNKSIZE);
        if((bp = extend_heap(extend / WSIZE)) == NULL){ // 메모리 없으면 extend 
            return NULL;
        }
        place(bp, size);
        return bp;
    }

}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp)); // 원래 ptr이었는데 bp로 통일 
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);

}

static void *coalesce(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    if(prev_alloc && next_alloc == 1){
        return bp;
    }

    else if(prev_alloc && !next_alloc){ // 뒤가 free일 때
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0)); // 위에서 header size를 바꿔서 NEXT_BLKP -> FTRP로 접근해야한다 

        return bp;
    }

    else if(!prev_alloc && next_alloc){ // 앞 청크가 free 일 때 
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

        return PREV_BLKP(bp);
    }

    else{
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 여기는 현재 header size를 안 건드려서 next_blkp 접근 가능 

        return PREV_BLKP(bp);
    }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
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

#define PACK(size, p_inuse, alloc) ((size) | ((p_inuse) << 1) | (alloc)) // prev_inuse 블록 추가 

#define GET(p) (*(unsigned int *)(p)) // int로 읽기 
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_P_INUSE(p) ((GET(p) & 0x2) >> 1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 정렬의 배수로 올림 (~0x7로 지움 구현)


static void *extend_heap(size_t words); // 함수 프로토타입 선언 
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t size);
static void change_prev(void *bp, size_t prev_alloc);

static char *heap_listp; // 전역변수로 포인터 선언 
static char* current; // next_fit 를 위한 전연변수 


/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{

    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1, 1)); // Prologue header
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1, 1)); // Prologue footer
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1, 1)); //  Epilogue header -> 처음은 헤더의 alloc과 같아야하니까 1로 시작해야함 
    heap_listp += 4 * WSIZE; // 첫 청크부터 시작하는 작은 최적화(처음에는 heap의 끝을 가르킴 )
    current = heap_listp - 2 * WSIZE; // next_fit 변수 초기화

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

    size_t isprev = GET_P_INUSE(HDRP(bp));

    PUT((char *)bp - WSIZE, PACK(size, isprev, 0)); // new chunk header
    PUT(FTRP(bp), PACK(size, isprev, 0));     // newe chunk footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1)); // epilogue

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
        size = ALIGN(size + WSIZE); // 올림 + 헤더
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




static void *find_fit(size_t asize)
{
    int flag = 0;
    current = NEXT_BLKP(current);

    if(GET_SIZE(HDRP(current)) == 0){ // 바로  Epilogue면 다음바퀴로
        current = heap_listp - DSIZE;
    }

    char * next;

    while(1){
        if(GET_SIZE(HDRP(current)) >= asize && !GET_ALLOC(HDRP(current))){
            return current; // 찾은 경우
        }

        next = NEXT_BLKP(current);

        if(GET_SIZE(HDRP(next)) == 0){ // 프롤로그를 만나면 다음바퀴로 감 
            current = heap_listp - DSIZE;
            if(flag){
                return NULL;
            }
            else{
                flag = 1;
            }
            continue;
        }
        current = next; // 아니면 다음 청크로
    }
}


// static void place(void *bp, size_t asize)
// {
//     if(GET_SIZE(HDRP(bp)) > asize){
//         asize = GET_SIZE(HDRP(bp));
//     }
//     PUT(HDRP(bp), PACK(asize, GET_P_INUSE(HDRP(bp)), 1));

// }

/* 푸터는 지금은 안 건드려도 됨 
static void change_prev(char *bp, size_t prev){
    size_t size = GET_SIZE(HDRP(bp));
    size_t alloc = GET_ALLOC(HDRP(bp));

    PUT(HDRP(bp), PACK(size, prev, alloc));
    
    if(alloc == 1){
        return;
    }

    PUT(FTRP(bp), PACK(size, prev, alloc));
}
*/

static void change_prev(void *bp, size_t prev_alloc){
    char *target = HDRP(bp);
    if(prev_alloc){
        PUT(target, GET(target) | 0x2); // prev_inuse 키기
    }
    else{
        PUT(target, GET(target) & ~0x2); // 끄기
    }
}

static void place(void *bp, size_t size){


    size_t total = GET_SIZE(HDRP(bp));
    size_t prev = GET_P_INUSE(HDRP(bp));

    if(total - size < 3 * WSIZE){
        PUT(HDRP(bp), PACK(total, prev, 1));
        change_prev(NEXT_BLKP(bp), 1);
        return; // 분할 햇다고 생각햇을 때 total-size 가 12바이트보다 작으면 그냥 return -> 이 조건에서 total == size인 경우도 잡아짐 
    }

    PUT(HDRP(bp), PACK(size, prev, 1));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(total-size, 1,  0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(total-size, 1, 0));


}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    if(bp == NULL){ // NULL이 들어오는 경우 안 터지게 방지
        return;
    }

    size_t size = GET_SIZE(HDRP(bp));
    size_t prev = GET_P_INUSE(HDRP(bp));

    PUT(HDRP(bp),  PACK(size, prev, 0));
    PUT(FTRP(bp),  PACK(size, prev, 0));
    change_prev(NEXT_BLKP(bp), 0);

    coalesce(bp);

}

//병합 함수 
static void *coalesce(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_P_INUSE(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    if(prev_alloc && next_alloc == 1){
        return bp;
    }

    else if(prev_alloc && !next_alloc){ // 뒤가 free일 때
        if(NEXT_BLKP(bp) == current){
            current = bp;
        }

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, prev_alloc, 0));
        PUT(FTRP(bp), PACK(size,prev_alloc, 0)); // 위에서 header size를 바꿔서 NEXT_BLKP -> FTRP로 접근해야한다 


        return bp;
    }

    else if(!prev_alloc && next_alloc){ // 앞 청크가 free 일 때 
        if (bp == current){
            current = PREV_BLKP(bp);
        }

        size_t prev = GET_P_INUSE(HDRP(PREV_BLKP(bp)));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev, 0));
        PUT(FTRP(bp), PACK(size, prev, 0));


    }

    else{
        if (bp == current || NEXT_BLKP(bp) == current){
            current = PREV_BLKP(bp);
        }

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        size_t prev = GET_P_INUSE(HDRP(PREV_BLKP(bp)));

        PUT(HDRP(PREV_BLKP(bp)), PACK(size, prev, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, prev, 0)); // 여기는 현재 header size를 안 건드려서 next_blkp 접근 가능 

    }


    return PREV_BLKP(bp);
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


    void *oldbp = bp;
    void *newbp;
    size_t copySize;

    newbp = mm_malloc(size);

    if (newbp == NULL)
        return NULL;

    if(bp == NULL){
        return newbp; // bp를 NULL로 넣은 경우엔 일반 malloc과 같음 
    }

    copySize = GET_SIZE(HDRP(oldbp)) - WSIZE; // footer가 없어져서 DSIZE -> WSIZE로 바꿈 
    if (size < copySize)
        copySize = size;

    memcpy(newbp, oldbp, copySize);
    mm_free(oldbp);

    return newbp;
}

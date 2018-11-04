/*
 * mm.c
 * xiezhiwen mail: zhiwenxie1900@outlook.com
 *
 * implict list, not much to say, wanna move to next lab, leave it for a moment
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "contracts.h"

#include "mm.h"
#include "memlib.h"


// Create aliases for driver tests
// DO NOT CHANGE THE FOLLOWING!
#ifdef DRIVER
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif

/*
 *  Logging Functions
 *  -----------------
 *  - dbg_printf acts like printf, but will not be run in a release build.
 *  - checkheap acts like mm_checkheap, but prints the line it failed on and
 *    exits if it fails.
 */

#ifndef NDEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#define checkheap(verbose) do {if (mm_checkheap(verbose)) {  \
                             printf("Checkheap failed on line %d\n", __LINE__);\
                             exit(-1);  \
                        }}while(0)
#else
#define dbg_printf(...)
#define checkheap(...)
#endif

/* 
 * Basic constants and macros
 */

#define WSIZE 4             // Word and header/footer size(bytes)
#define DSIZE 8             // Double word size(bytes)
#define CHUNKSIZE (1<<12)   // Extend heap by this amount(bytes)

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

// pack a size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// Read and write a word at address p
#define GET(p) (*(uint32_t *)(p))
#define PUT(p, val) (*(uint32_t *)(p) = (val))

// Read size and allocated field form header or footer
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// Given block ptr block, compute address of its header and footer
#define HDRP(block) ((uint32_t *)(block))
#define FTRP(block) ((uint32_t *)(block) + (GET_SIZE(block) >> 2) - 1)

// Given block ptr block, get previous/next block
#define NEXT_BLOCK(block) ((uint32_t *)(block) + (GET_SIZE(block) >> 2))
#define PREV_BLOCK(block) ((uint32_t *)(block) - (GET_SIZE((uint32_t *)(block) - 1) >> 2))

// Given block memory ptr bmp, get block ptr;
#define BLOCK(bmp) ((uint32_t *)(bmp) - 1) 


// Private global varible
static uint32_t* heap_listp;


/*
 *  Helper functions
 *  ----------------
 */

// Align p to a multiple of w bytes
static inline void* align(const void const* p, unsigned char w) {
    return (void*)(((uintptr_t)(p) + (w-1)) & ~(w-1));
}

// Check if the given pointer is 8-byte aligned
static inline int aligned(const void const* p) {
    return align(p, 8) == p;
}

// Return whether the pointer is in the heap.
static inline int in_heap(const void* p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}


/*
 *  Block Functions
 *  ---------------
 *  The functions below act similar to the macros in the book, calculate
 *  size in multiples of 4 bytes, and check the validity of pointer passed in.
 */

// Return the size of the given block in bytes
static inline size_t block_size(const uint32_t* block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return GET_SIZE(block);
}

// Return true if the block is allocted, false otherwise
static inline int block_alloc(const uint32_t* block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return GET_ALLOC(block);
}

// Mark the given block as free(0)/alloced(1) by marking the header and footer.
// And set the size if given a non-zero size argument.
static inline void block_mark(uint32_t* block, int alloced, size_t size) {
    size_t next;
    size_t mark;
    REQUIRES(block != NULL);
    

    if (size == 0){
        size = block_size(block);  
    }
    next = (size - WSIZE);
    mark = alloced ? (size | 0x1) : (size & ~0x7);
    PUT(block, mark);
    // REQUIRES(in_heap((char*)block + next)); // check whether the size is valid
    PUT((char*)block + next, mark);
    
    
}

// Return a pointer to the memory malloc should return
static inline uint32_t* block_mem(uint32_t* const block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));
    REQUIRES(aligned(block + 1));

    return block + 1;
}

// Return the header to the previous block
static inline uint32_t* block_prev(uint32_t* const block) {
    uint32_t* bp;
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));
    
    bp = PREV_BLOCK(block);
    REQUIRES(in_heap(bp));
    return bp;
}

// Return the header to the next block
static inline uint32_t* block_next(uint32_t* const block) {
    uint32_t* bp;
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));
    
    bp = NEXT_BLOCK(block);
    REQUIRES(in_heap(bp));
    return bp;
}

// coalesce free blocks
static uint32_t* coalesce(uint32_t *block)
{
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));
    REQUIRES(aligned(block + 1));

    uint32_t* prev_bp = block_prev(block);
    uint32_t* next_bp = block_next(block);
    
    int prev_alloc = block_alloc(prev_bp);
    int next_alloc = block_alloc(next_bp);
    
    size_t size = block_size(block);
    if (prev_alloc && next_alloc) {
        return block;
    } 

    else if (prev_alloc && !next_alloc) {
        size += block_size(next_bp);
        PUT(FTRP(next_bp), PACK(size, 0));
        PUT(block, PACK(size, 0));
        return block;
    } 
    
    else if (!prev_alloc && next_alloc) {
        size += block_size(prev_bp);
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(block), PACK(size, 0));
        return prev_bp;
    }

    else {
        size += block_size(prev_bp) + block_size(next_bp);
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        return prev_bp;
    }
}
// extend  the heap with a new free block
static uint32_t* extend_heap(size_t words)
{
    size_t size;
    uint32_t* block;  // block pointer
    // Allocte an even number of words to maintain alignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((block = mem_sbrk(size)) == (void *)-1) 
        return NULL;
    REQUIRES(aligned(block));   // check validity of alignment
    block--;                        // get to orignal epologue header
    block_mark(block, 0, size);
    // dbg_printf("after extend, block %p size: %lu, should be %lu\n", 
    //             (void*)block, block_size(block), size);
    PUT(NEXT_BLOCK(block), PACK(0, 1));   // New epilogue header
    // checkheap(1);
    return coalesce(block);
}

// find first fit in implict free list
static void* find_fit(size_t asize)
{   
    for (uint32_t* bp = heap_listp; block_size(bp) > 0; bp = block_next(bp)){
        if (!block_alloc(bp) && block_size(bp) >= asize)
            return bp;
    }
    return NULL;
}

// set allocked block in the heap and split block when possible
static void place(uint32_t *block, size_t asize)
{
    size_t fsize = block_size(block);
    size_t remain = fsize - asize;
    if (remain < 4 * WSIZE) { // not enough space to split
        block_mark(block, 1, fsize);
    } else {
        block_mark(block, 1, asize);
        block = block_next(block);
        block_mark(block, 0, remain);
    }

}
/*
 *  Malloc Implementation
 *  ---------------------
 *  The following functions deal with the user-facing malloc implementation.
 */

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) 
        return -1;
    PUT(heap_listp, 0);                      // Alignment padding
    PUT(heap_listp + 1, PACK(DSIZE, 1));     // Prologue header
    PUT(heap_listp + 2, PACK(DSIZE, 1));     // Prologue footer
    PUT(heap_listp + 3, PACK(0, 1));         // Epilogue header
    heap_listp += 1;

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * malloc
 */

void *malloc (size_t size) {
    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit
    uint32_t *block;
    // checkheap(1);      // Let's make sure the heap is ok!
    
    if (size == 0)
        return NULL;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else // uprounded size plus overhead
        asize = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE); 

    if ((block = find_fit(asize)) == NULL) { // No fit found. Get more memory
        extendsize = MAX(asize, CHUNKSIZE);
        // dbg_printf("extend size : %lu\n", extendsize);
        if ((block = extend_heap(extendsize/WSIZE)) == NULL) // extend heap failed
            return NULL;
    }
    place(block, asize);
    // checkheap(1);
    return block_mem(block);
}

/*
 * free
 */
void free (void *ptr) {
    // checkheap(1);
    if (ptr == NULL) {
        return;
    }
    uint32_t* block = BLOCK(ptr); // get block pointer from ptr to memory
    block_mark(block, 0, 0); // mark alloc bit to 0, don't change size
    coalesce(block);
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    // checkheap(1);
    void *newptr;
    size_t oldsize;
    if (size == 0) {
        free(oldptr);
        return NULL;
    }
    if (oldptr == NULL) {
        return malloc(size);
    }
    
    newptr = malloc(size);
    if (newptr == NULL) { // realloc failed
        return NULL;
    }
    oldsize = MIN(GET_SIZE(BLOCK(oldptr)), size);
    memcpy(newptr, oldptr, oldsize);

    free(oldptr);
    // checkheap(1);
    return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *ptr = malloc(bytes);
    if (ptr != NULL) {
        memset(ptr, 0 , bytes);
        return ptr;
    }
    return NULL;
}

// Returns 0 if no errors were found, otherwise returns the error
int mm_checkheap(int verbose) {
    uint32_t* bp = heap_listp;
    size_t size;
    
    size_t sums = 0;
    while (in_heap(bp)) {
        size = block_size(bp);
        sums += size;
        if (verbose > 2){
            dbg_printf("current block pointer: %p\t size: %lu\n", (void*)bp, size);
        }
        if (size == 0){
            dbg_printf("heap maybe normal, current position: %lu/%ld\n",
                sums, (bp - heap_listp)*WSIZE);
            return 0;
        }
        bp = NEXT_BLOCK(bp);
    }
    dbg_printf("failed on block pointer %p\n", (void*)bp);
    return -1;
}




























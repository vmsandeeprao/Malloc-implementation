/*
 * mm.c
 * svadlapu - Sandeep Vadlaputi 
 *
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

#define WSIZE (int) 4
#define DSIZE (int) 8
#define CHUNKSIZE (int) (1<<12)
#define GET(p) (*(int *) (p))
#define PUT(p, val) (*(int *) (p) = (val))
#define PACK(size, alloc) (size | alloc)
#define GET_SIZE(p) ((int) (GET(p) & ~0x7))
#define GET_ALLOC(p) ((int) (GET(p) & 0x1))
#define HDRP(bp) ((char *) (bp) - WSIZE)
#define FTRP(bp) ((char *) (bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *) (bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *) (bp) - GET_SIZE(HDRP(bp) - WSIZE))

char *heap_start;                        

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

/*
// Return whether the pointer is in the heap.
static int in_heap(const void* p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}
*/

/*
 *  Block Functions
 *  ---------------
 *  TODO: Add your comment describing block functions here.
 *  The functions below act similar to the macros in the book, but calculate
 *  size in multiples of 4 bytes.
 */

/*
// Return the size of the given block in multiples of the word size
static inline unsigned int block_size(const uint32_t* block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return (block[0] & 0x3FFFFFFF);
}

// Return true if the block is free, false otherwise
static inline int block_free(const uint32_t* block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return !(block[0] & 0x40000000);
}

// Mark the given block as free(1)/alloced(0) by marking the header and footer.
static inline void block_mark(uint32_t* block, int free) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    unsigned int next = block_size(block) + 1;
    block[0] = free ? block[0] & (int) 0xBFFFFFFF : block[0] | 0x40000000;
    block[next] = block[0];
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
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return block - block_size(block - 1) - 2;
}

// Return the header to the next block
static inline uint32_t* block_next(uint32_t* const block) {
    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));

    return block + block_size(block) + 2;
}
*/

void printheap() {
    char *ptr;
    int i = 0;
    ptr = heap_start;
    printf("\nPrinting heap:");
    while(GET_SIZE(HDRP(ptr)) != 0) {
        printf("\nBlock%d: \n", i);
        printf("block address: %p\n", ptr);
        printf("header address: %p\n", HDRP(ptr));
        printf("footer address: %p\n", FTRP(ptr));
        printf("contents of header: %d\n", (int)GET(HDRP(ptr)));
        printf("contents of footer: %d\n", (int)GET(FTRP(ptr)));
        printf("size: %d\n", (int)GET_SIZE(HDRP(ptr)));
        printf("alloc: %d\n", (int)GET_ALLOC(HDRP(ptr)));
        printf("address of next block: %p\n\n", NEXT_BLKP(ptr));
        ptr = NEXT_BLKP(ptr);
        i++;
    }
}

static void *coalesce(void *bp) {

    //printf("\nEntering coalesce()...\n");

    int next = (int)GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    int prev = (int)GET_ALLOC(FTRP(PREV_BLKP(bp)));
    
    if(prev && next) {
        //printf("Case1: Both neighbours are allocated\n");
        return bp;
    }

    else if(!prev && next) {
        //printf("Case2: prev is free\n");
        //printf("1: %d 2: %d\n", GET_SIZE(HDRP(bp)), GET_SIZE(HDRP(PREV_BLKP(bp))));
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(PREV_BLKP(bp)));
        //printf("New size: %d bytes\n", size);
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(bp), PACK(size, 0));
        return PREV_BLKP(bp);
    }

    else if(prev && !next) {
        //printf("Case3: next is free\n"); 
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        //printf("New size: %d\n", size);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    }

    else {
        //printf("Case4: both are free\n");
        //printf("bp: %p\n", bp);
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(PREV_BLKP(bp))); 
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        //printf("New size: %d bytes\n", size);
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        //printf("Updated header of previous block\n");
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        //printf("Updated footer of next block\n");
        //printf("Returning header of prev block. Exiting coalesce()...\n");
        return PREV_BLKP(bp);
    }
}

static void *extend_heap(int words) {
    char *bp;
    int size;

    //printf("\nEntering extend_heap()...\n");
    //printf("Requested extend size: %d bytes\n", words*WSIZE);

    //Round up if not double-word aligned
    if(words % 2)
        size = (words+1)*WSIZE;
    else size = words*WSIZE;
    //printf("Rounded up for double-word alignment\n");

    //printf("Requesting memory from mem_sbrk...\n");
    if((bp = mem_sbrk(size)) == (void *)-1) {
        //printf("mem_sbrk failed. Exiting extend_heap()...\n");
        return NULL;
    }
    //printf("mem_sbrk ran well..\n");

    PUT(HDRP(bp), PACK(size, 0));
    //printf("Added header to new block: %d\n", (int)GET(HDRP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    //printf("Added footer to new block: %d\n", (int)GET(FTRP(bp)));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    //printf("Added epilogue to new block: %d\n", (int)GET(HDRP(NEXT_BLKP(bp))));

    //printf("Calling coalesce and exiting extend_heap()...\n");
    return coalesce(bp);
}

static void *find_fit(int size) {
    void *ptr;
    //printf("\nEntering find_fit()...\n");
    //printf("Requested fit size: %d bytes\n", size);
    ptr = heap_start;
    while(GET_SIZE(HDRP(ptr)) > 0) {
        //printf("\nAddress: %p\n", ptr);
        //printf("Size: %d\n", GET_SIZE(HDRP(ptr)));
        if(GET_SIZE(HDRP(ptr)) >= size && !GET_ALLOC(HDRP(ptr))) {
            //printf("Found a fit!\n");
            //printf("Exiting find_fit()...\n");
            return ptr;
        }
        //printf("Not a good fit.. Move on!\n");
        ptr = NEXT_BLKP(ptr);
    }
    //printf("Did not find any fit. Exiting find_fit()...\n");
    return NULL;
}

static void place(void *bp, int size) {
    //printf("\nEntering place()...\n");
    //printf("Allocating %d bytes\n", size);
    //printf("Size at bp: %d bytes\n", GET_SIZE(HDRP(bp)));
    //printf("Alloc at bp: %d\n", GET_ALLOC(HDRP(bp)));
    if(GET_ALLOC(HDRP(bp)) || (int)GET_SIZE(HDRP(bp)) < size) {
        //printf("Invalid input. Exiting place()...\n");
        return;
    }
    int extra = GET_SIZE(HDRP(bp)) - size;
    PUT(HDRP(bp), PACK(size, 1));
    //printf("Updated block header\n");
    PUT(FTRP(bp), PACK(size, 1));
    //printf("Updated block footer\n");
    if(extra > 0) {
        //printf("Leftover bytes seen - Splitting block.\n");
        //printf("Extra bytes: %d bytes\n", extra);
        PUT(HDRP(NEXT_BLKP(bp)), PACK(extra, 0));
        //printf("Updated new block header\n");
        PUT(FTRP(NEXT_BLKP(bp)), PACK(extra, 0));
        //printf("Updated new block footer\n");
    }
    //printf("Exiting place()...\n");
    return;
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
    //printf("\nEntering mm_init()...\n");
    if((heap_start = mem_sbrk(4*WSIZE)) == NULL) {
        //printf("Error in mem_sbrk. Exiting mm_init...\n");
        return -1;
    }
    PUT(heap_start, 0); //padding
    //printf("Added init padding\n");
    PUT(heap_start+WSIZE, PACK(DSIZE, 1)); //prologue header
    //printf("Added prologue header\n");
    PUT(heap_start+DSIZE, PACK(DSIZE, 1)); //prologue footer
    //printf("Added prologue footer\n");
    PUT(heap_start+WSIZE+DSIZE, PACK(0, 1)); //epilogue
    //printf("Added epilogue\n");
    heap_start += DSIZE;
    //printf("Adjusted heap_start pointer\n");

    //printf("Extending heap..\n");
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        //printf("Extending heap failed. Exiting mm_init()...\n");
        return -1;
    }

    //printf("Exiting mm_init() normally...\n");
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    //printf("\nEntering malloc()...\n");
    //printf("Requested size: %d bytes\n", (int)size);
    void *bp;
    checkheap(1);  // Let's make sure the heap is ok!
    int new_size,extend_size;

    if((int)size < 0) {
        //printf("Invalid size entered. Exiting malloc()...\n");
        return NULL;
    }

    if(size <= DSIZE) new_size = 2*DSIZE;
    else new_size = ((size + 2*DSIZE - 1)/DSIZE)*DSIZE;
    //printf("Adjusted size. New size: %d bytes\n", new_size);

    if((bp = find_fit(new_size)) != NULL) {
        place(bp, new_size);
        //printf("Found new fit and allocated block\n");
        return bp;
    }

    extend_size = (new_size > CHUNKSIZE) ? new_size : CHUNKSIZE;
    //printf("Extend size: %d\n", extend_size);
    if((bp = extend_heap(extend_size/WSIZE)) == NULL)
        return NULL;
    //printf("Extended heap to accommodate request\n");
    place(bp, new_size);
    //printf("Allocated block.\n");
    //printf("Exiting malloc()...\n");
    return bp;
}

/*
 * free
 */
void free (void *ptr) {
    //printf("\nEntering free()...\n");
    if (ptr == NULL) {
        //printf("NULL ptr. Exiting free()...\n");
        return;
    }
    int size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    //printf("Updated block header\n");
    PUT(FTRP(ptr), PACK(size, 0));
    //printf("Updated block footer\n");
    coalesce(ptr);
    //printf("Coalesced neighbouring free blocks\n");
    //printf("Exiting free()...\n");
    return;
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    if(oldptr == NULL) return malloc(size);
    if(size == 0) {
        free(oldptr);
        return NULL;
    }

    void *ptr = malloc(size);

    if(!ptr) return NULL;

    int oldsize = GET_SIZE(HDRP(oldptr));
    if((int)size < oldsize) oldsize = size;
    memcpy(ptr, oldptr, oldsize);

    free(oldptr);

    return ptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 */
void *calloc (size_t nmemb, size_t size) {
    size_t total = nmemb*size;
    void *ptr = malloc(total);
    memset(ptr, 0, total);
    return ptr;
}

// Returns 0 if no errors were found, otherwise returns the error
int mm_checkheap(int verbose) {
    verbose = verbose;
    return 0;
}

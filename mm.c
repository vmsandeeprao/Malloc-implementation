/*
 * mm.c
 * 
 * svadlapu - Sandeep Vadlaputi 
 * 
 * Abstract: This allocator uses segregated lists (as powers of 2) with a first 
 *           fit algorithm to serve malloc(), free(), realloc() and calloc() 
 *           requests.
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
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

//Generic macros
#define WSIZE (int) 4 //Word size (in bytes)
#define DSIZE (int) 8 //Double-word size (in bytes)
#define CHUNKSIZE (int) 528 //for extend_heap() (in bytes) 
#define OVERHEAD (int) 16 //Header, footer, prev_free and next_free addresses 
#define BUCKETS (int) 16 //Number of buckets for seg_list      

//Pointers to start of heap and start of explicit free list
char *heap_start = NULL;
char *ref = (char *) 0x800000000;
char **seg_list = NULL;

//Block level functions


//Get value at address
static inline int GET(char *p) {
    return * (int *) p;
}

//Put value at address
static inline void PUT(char *p, int val) {
    * (int *) p = val;
    return;
}

//Pack size and alloc bit
static inline int PACK(int size, int alloc) {
    return size | alloc;
}

//Get size of a block
static inline int GET_SIZE(char *p) {
    return (int) (GET(p) & ~0x7);
}

//Get alloc of a block
static inline int GET_ALLOC(char *p) {
    return (int) (GET(p) & 0x1);
}

//Get address of a block header
static inline char* HDRP(char *bp) {
    return bp - WSIZE;
}

//Get address of a block footer
static inline char* FTRP(char *bp) {
    return bp + GET_SIZE(HDRP(bp)) - DSIZE;
}

//Get address of next block
static inline char* NEXT_BLKP(char *bp) {
    return bp + GET_SIZE(HDRP(bp));
}

//Get address of previous block
static inline char* PREV_BLKP(char *bp) {
    return bp - GET_SIZE(HDRP(bp) - WSIZE);
}

//Get address of previous free block
static inline char* GET_PREV_FREE(char *bp) {
    unsigned int val = *(unsigned int *) bp;
    if(val == (unsigned int) -1) return NULL;
    return (char *) ref + val;
}

//Store address of previous free block - as an unsigned int
static inline void PUT_PREV_FREE(char *bp, char *addr) {
    unsigned int val;
    if(addr == NULL) val = (unsigned int) -1;
    else val = (unsigned int) (addr - ref);
    * (unsigned int *) bp = val;
    return;
}

//Get address of next free block
static inline char* GET_NEXT_FREE(char *bp) {
    unsigned int val = * (unsigned int *) (bp + WSIZE);
    if(val == (unsigned int) -1) return NULL;
    return (char *) ref + val;
}

//Store address of next free block - as an unsigned int
static inline void PUT_NEXT_FREE(char *bp, char *addr) {
    unsigned int val;
    if(addr == NULL) val = (unsigned int) -1;
    else val = (unsigned int) (addr - ref);
    * (unsigned int *) ((char *) bp + WSIZE) = val;
    return;
}

//Pack the alloc bit of previous block given the address of the block
static inline void PACK_PREV_ALLOC(char *p, int alloc) {
    int val;
    if(alloc) val = GET(HDRP(p)) | 0x2;
    else val = GET(HDRP(p)) & ~0x2;
    PUT(HDRP(p), val);
}

//Get the alloc bit of previous block given the address of the block
static inline int GET_PREV_ALLOC(char *p) {
    if(GET(HDRP(p)) & 0x2) return 1;
    else return 0;
}

/*
 *  Helper functions
 *  ----------------
 */

// Return whether the pointer is in the heap.
static int in_heap(const void* p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

//Print the entire heap - for debug purposes
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

//Print entire free lists - for debug purposes
void printfree() {
    dbg_printf("\nPrinting free list:\n");
    for(int i=0;i<BUCKETS;i++) {
        char *ptr = seg_list[i];
        if(ptr == NULL) printf("No free blocks in this list.\n");
        while(ptr != NULL) {
            printf("\nBlock: \n");
            printf("block address: %p\n", ptr);
            printf("header address: %p\n", HDRP(ptr));
            printf("footer address: %p\n", FTRP(ptr));
            printf("contents of header: %d\n", (int)GET(HDRP(ptr)));
            printf("contents of footer: %d\n", (int)GET(FTRP(ptr)));
            printf("size: %d\n", (int)GET_SIZE(HDRP(ptr)));
            printf("alloc: %d\n", (int)GET_ALLOC(HDRP(ptr)));
            printf("Prev free block at: %p\n", GET_PREV_FREE(ptr));
            printf("Next free block at: %p\n", GET_NEXT_FREE(ptr));
            ptr = GET_NEXT_FREE(ptr);
        }
    }
    printf("\n");
}

//Print a specific block - for debug and checkheap purposes
void printone(void *ptr) {
    ptr = ptr;
    dbg_printf("\nPrinting block with details:\n");
    dbg_printf("block address: %p\n", ptr);
    dbg_printf("header address: %p\n", HDRP(ptr));
    dbg_printf("footer address: %p\n", FTRP(ptr));
    dbg_printf("contents of header: %d\n", (int)GET(HDRP(ptr)));
    dbg_printf("contents of footer: %d\n", (int)GET(FTRP(ptr)));
    dbg_printf("size: %d\n", (int)GET_SIZE(HDRP(ptr)));
    dbg_printf("alloc: %d\n", (int)GET_ALLOC(HDRP(ptr)));
    dbg_printf("prev_alloc of current block: %d\n", GET_PREV_ALLOC(ptr));
    dbg_printf("prev_alloc of next block: %d\n", GET_PREV_ALLOC(NEXT_BLKP(ptr)));
    dbg_printf("Prev free block at: %p\n", GET_PREV_FREE(ptr));
    dbg_printf("Next free block at: %p\n", GET_NEXT_FREE(ptr));
    dbg_printf("address of next block: %p\n\n", NEXT_BLKP(ptr));
    dbg_printf("\n");
}

//Find index in the segregated list with size
int find_index(int size) {
    for(int i=0;i<BUCKETS;i++) 
        if(size <= 1<<i)
            return i;
    return BUCKETS - 1;
}

//Insert a free block at the start of the free list
static void insert_free(void *bp) {
    dbg_printf("\nEntering insert_free()...\n");
    dbg_printf("Inserting free block with address: %p\n", bp);
    dbg_printf("Block size: %d\n", GET_SIZE(HDRP(bp)));
    
    int size = GET_SIZE(HDRP(bp));
    int seg_index = find_index(size);
    dbg_printf("seg_index: %d\n", seg_index);

    dbg_printf("Address of list: %p\n", seg_list[seg_index]);

    PUT_NEXT_FREE(bp, seg_list[seg_index]);
    PUT_PREV_FREE(bp, NULL);
    
    if(seg_list[seg_index] != NULL) PUT_PREV_FREE(seg_list[seg_index], bp);
    seg_list[seg_index] = bp;

    PACK_PREV_ALLOC(NEXT_BLKP(bp), 0);
    dbg_printf("Updated prev_alloc bit in next block to 0 (free): %d\n", GET_PREV_ALLOC(NEXT_BLKP(bp)));

    PUT(FTRP(bp), GET(HDRP(bp)));
    dbg_printf("Updated footer to reflect new prev_alloc data");

    dbg_printf("New address of list: %p\n", seg_list[seg_index]);    
    dbg_printf("Added block.\n");
    dbg_printf("Exiting insert_free()...\n");
    return;
    
}

//Pop a free block from the free list
static void pop_free(void *bp) {
    dbg_printf("\nEntering pop_free()...\n");
    dbg_printf("Popping free block with address: %p\n", bp);
    dbg_printf("Block size: %d\n", GET_SIZE(HDRP(bp)));

    int size = GET_SIZE(HDRP(bp));
    int seg_index = find_index(size);
    dbg_printf("seg_index: %d\n", seg_index);

    dbg_printf("Address of list: %p\n", seg_list[seg_index]);
    
    if(GET_PREV_FREE(bp) == NULL) {
        seg_list[seg_index] = GET_NEXT_FREE(bp);
        dbg_printf("First block. Changed seg_list[seg_index].\n");
    }
    
    else {
        PUT_NEXT_FREE(GET_PREV_FREE(bp), GET_NEXT_FREE(bp));
        dbg_printf("Non-first block. Changed next_free of prev block.\n");
    }
    
    if(GET_NEXT_FREE(bp) != NULL) {
        PUT_PREV_FREE(GET_NEXT_FREE(bp), GET_PREV_FREE(bp));
        dbg_printf("Non-last block. Changed prev_free for next block.\n");
    }

    PACK_PREV_ALLOC(NEXT_BLKP(bp), 1);
    dbg_printf("Updated prev_alloc bit in next block to 1 (allocated).\n");

    dbg_printf("Exiting pop_free()...\n");
    return;
}

//Coalesce with neighbouring free blocks
static void *coalesce(void *bp) {

    dbg_printf("\nEntering coalesce()...\n");
    
    int next = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    int prev = GET_PREV_ALLOC(bp);
    
    //Case 1
    if(prev && next) {
        dbg_printf("Case1: Both neighbours are allocated\n");
        insert_free(bp);
        return bp;
    }

    //Case 2
    else if(!prev && next) {
        dbg_printf("Case2: prev is free\n");
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(PREV_BLKP(bp)));
        dbg_printf("New size: %d bytes\n", size);

        bp = PREV_BLKP(bp);
        int alloc = GET_PREV_ALLOC(bp);
        dbg_printf("Obtained old prev_alloc of prev block: %d\n", alloc);

        pop_free(bp);
        PUT(HDRP(bp), PACK(size,0));
        PACK_PREV_ALLOC(bp, alloc);
        dbg_printf("Restored old prev_alloc to prev block.\n");
        
        insert_free(bp);
        return bp;
    }

    //Case 3
    else if(prev && !next) {
        dbg_printf("Case3: next is free\n"); 
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        dbg_printf("New size: %d\n", size);

        int alloc = GET_PREV_ALLOC(bp);
        dbg_printf("Obtained old prev_alloc of block: %d\n", alloc);

        pop_free(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PACK_PREV_ALLOC(bp, alloc);
        dbg_printf("Restored old prev_alloc to block.\n");
        
        insert_free(bp);
        return bp;
    }

    //Case 4
    else {
        dbg_printf("Case4: both are free\n");
        dbg_printf("bp: %p\n", bp);
        int size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(PREV_BLKP(bp))); 
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        dbg_printf("New size: %d bytes\n", size);

        int alloc = GET_PREV_ALLOC(PREV_BLKP(bp));
        dbg_printf("Obtained old prev_alloc of prev block: %d\n", alloc);

        pop_free(PREV_BLKP(bp));
        pop_free(NEXT_BLKP(bp));
        bp = PREV_BLKP(bp);
        
        PUT(HDRP(bp), PACK(size, 0));
        dbg_printf("Updated header of previous block\n");

        PACK_PREV_ALLOC(bp, alloc);
        dbg_printf("Restored old prev_alloc to prev block.\n");

        dbg_printf("Returning header of prev block. Exiting coalesce()...\n");

        insert_free(bp);
        return bp;
    }
}

//Request more heap space when process has run out of space
static void *extend_heap(int words) {
    char *bp;
    int size;
    int alloc; //Storing the alloc bit of previous block

    dbg_printf("\nEntering extend_heap()...\n");
    dbg_printf("Requested extend size: %d bytes\n", words*WSIZE);

    //Round up if not double-word aligned
    if(words % 2)
        size = (words+1)*WSIZE;
    else size = words*WSIZE;
    dbg_printf("Rounded up for double-word alignment\n");

    dbg_printf("Requesting memory from mem_sbrk...\n");
    if((bp = mem_sbrk(size)) == (void *)-1) {
        dbg_printf("mem_sbrk failed. Exiting extend_heap()...\n");
        return NULL;
    }
    dbg_printf("mem_sbrk ran well..\n");
    dbg_printf("Obtained new memory at location: %p\n", bp);
    
    alloc = GET_PREV_ALLOC(bp);
    dbg_printf("prev_alloc: %d\n", alloc);

    PUT(HDRP(bp), PACK(size, 0));
    dbg_printf("Added header to new block: %d\n", (int)GET(HDRP(bp)));
    
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    dbg_printf("Added epilogue block: %d\n", (int)GET(HDRP(NEXT_BLKP(bp))));
    
    PACK_PREV_ALLOC(bp, alloc);
    dbg_printf("Restored alloc bit of previous block to %d\n", alloc);

    insert_free(bp);
    dbg_printf("Exiting extend_heap()...\n");
    return bp;
}

//Find an ideal candidate free block to serve malloc request
static void *find_fit(int size) {
    dbg_printf("\nEntering find_fit()...\n");
    dbg_printf("Requested fit size: %d bytes\n", size);
    
    int seg_index = find_index(size);
    dbg_printf("Starting with seg_index: %d\n", seg_index);

    for(int i=seg_index;i<BUCKETS;i++) {
        dbg_printf("seg_index: %d\n", i);        
        char *ptr = seg_list[i];
        dbg_printf("Address of list: %p\n", ptr);
        while(ptr != NULL) {
            dbg_printf("\nAddress: %p\n", ptr);
            dbg_printf("Size: %d\n", GET_SIZE(HDRP(ptr)));
            if(GET_SIZE(HDRP(ptr)) >= size && !GET_ALLOC(HDRP(ptr))) {
                dbg_printf("Found a fit!\n");
                dbg_printf("Exiting find_fit()...\n");
                return ptr;
            }
            dbg_printf("Not a good fit.. Move on!\n");
            ptr = GET_NEXT_FREE(ptr);
        }
    }

    dbg_printf("Did not find any fit. Exiting find_fit()...\n");
    return NULL;
}

//Allocate the candidate block 
static void place(void *bp, int size) {
    dbg_printf("\nEntering place()...\n");
    dbg_printf("Allocating %d bytes\n", size);
    dbg_printf("Size at bp: %d bytes\n", GET_SIZE(HDRP(bp)));
    dbg_printf("Alloc at bp: %d\n", GET_ALLOC(HDRP(bp)));
    if(GET_ALLOC(HDRP(bp)) || (int)GET_SIZE(HDRP(bp)) < size) {
        dbg_printf("Invalid input. Exiting place()...\n");
        return;
    }

    int alloc = GET_PREV_ALLOC(bp);
    dbg_printf("Obtained old prev_alloc of block: %d\n", alloc);
    
    int extra = GET_SIZE(HDRP(bp)) - size;
    if(extra >= OVERHEAD) {
        pop_free(bp);
        PUT(HDRP(bp), PACK(size, 1));
        dbg_printf("Updated block header\n");
        PACK_PREV_ALLOC(bp, alloc);
        dbg_printf("Restored old prev_alloc to shrunk-down block.\n");

        dbg_printf("Leftover bytes seen - Splitting block.\n");
        dbg_printf("Extra bytes: %d bytes\n", extra);
        PUT(HDRP(NEXT_BLKP(bp)), PACK(extra, 0));
        dbg_printf("Updated new block header\n");
        
        PACK_PREV_ALLOC(NEXT_BLKP(bp), 1);
        dbg_printf("Updated prev_alloc in next block: %d\n", GET_PREV_ALLOC(NEXT_BLKP(bp)));
        
        insert_free(NEXT_BLKP(bp));
    }
    else {
        PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
        dbg_printf("Updated block header\n");
        
        PACK_PREV_ALLOC(bp, alloc);
        dbg_printf("Restored old prev_alloc to block.\n");
        pop_free(bp);
    }

    dbg_printf("Exiting place()...\n");
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
    dbg_printf("\nEntering mm_init()...\n");
    
    seg_list = NULL;
    //Allocating memory for seg_list
    if((seg_list = mem_sbrk(32*WSIZE)) == NULL) {
        dbg_printf("Error in mem_sbrk. Exiting mm_init...\n");
        return -1;
    }

    //Clearing BUCKETS elements for the seg_list
    for(int i=0;i<BUCKETS;i++) seg_list[i] = NULL;

    //Allocating memory for heap
    if((heap_start = mem_sbrk(4*WSIZE)) == NULL) {
        dbg_printf("Error in mem_sbrk. Exiting mm_init...\n");
        return -1;
    }

    //Initializing heap
    PUT(heap_start, 0); //padding
    dbg_printf("Added init padding\n");
    PUT(heap_start+WSIZE, PACK(DSIZE, 1)); //prologue header
    dbg_printf("Added prologue header\n");
    PUT(heap_start+DSIZE, PACK(DSIZE, 1)); //prologue footer
    dbg_printf("Added prologue footer\n");
    PUT(heap_start+WSIZE+DSIZE, PACK(0, 1)); //epilogue
    dbg_printf("Added epilogue\n");
    PACK_PREV_ALLOC(heap_start+DSIZE+DSIZE, 1);
    dbg_printf("Added prev_alloc bit to epilogue block: %d\n", GET_PREV_ALLOC(heap_start+DSIZE+DSIZE));
    dbg_printf("Epilogue addr: %p\n", heap_start+WSIZE+DSIZE);

    heap_start += DSIZE;
    dbg_printf("Adjusted heap_start pointer\n");

    dbg_printf("Extending heap..\n");
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        dbg_printf("Extending heap failed. Exiting mm_init()...\n");
        return -1;
    }
    //checkheap(1);
    dbg_printf("Exiting mm_init() normally...\n");
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    dbg_printf("\nEntering MALLOC()...\n");
    dbg_printf("Requested size: %d bytes\n", (int)size);
    void *bp;
    //checkheap(1);  // Let's make sure the heap is ok!
    int new_size,extend_size;

    if((int)size < 0) {
        dbg_printf("Invalid size entered. Exiting malloc()...\n");
        return NULL;
    }

    new_size = size + WSIZE; //Adding header overhead
    new_size = ((new_size + DSIZE - 1) / DSIZE) * DSIZE; //Aligning to 8 bytes
    if(new_size < OVERHEAD) new_size = OVERHEAD;

    dbg_printf("Adjusted size. New size: %d bytes\n", new_size);

    if((bp = find_fit(new_size)) != NULL) {
        place(bp, new_size);
        dbg_printf("Found new fit and allocated block. Exiting malloc()...\n");
        return bp;
    }

    extend_size = (new_size > CHUNKSIZE) ? new_size : CHUNKSIZE;
    dbg_printf("Extend size: %d\n", extend_size);
    if((bp = extend_heap(extend_size/WSIZE)) == NULL)
        return NULL;
    dbg_printf("Extended heap to accommodate request\n");
    place(bp, new_size);
    
    dbg_printf("Allocated block.\n");
    dbg_printf("Exiting MALLOC()...\n");
    return bp;
}

/*
 * free
 */
void free (void *ptr) {
    dbg_printf("\nEntering FREE()...\n");
    if (ptr == NULL) {
        dbg_printf("NULL ptr. Exiting free()...\n");
        return;
    }
    dbg_printf("Requesting to free %p\n", ptr);
    dbg_printf("Size of block: %d\n", GET_SIZE(HDRP(ptr)));

    int alloc = GET_PREV_ALLOC(ptr);
    dbg_printf("Obtained old prev_alloc of block: %d\n", alloc);

    int size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    dbg_printf("Updated block header\n");
    PACK_PREV_ALLOC(ptr, alloc);
    dbg_printf("Restored old prev_alloc to block.\n");

    coalesce(ptr);
    dbg_printf("Coalesced neighbouring free blocks\n");
    dbg_printf("Exiting FREE()...\n");
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
/* Checkheap() performs the following checks:
1) Block level:
        1. Header and footer match
        2. Payload area is aligned (8!)
2) List level:
        1. Next/prev pointers in consecutive free blocks are consistent
        2. Free list contains no allocated blocks
        3. All free blocks are in the free list 
        4. No cycles in the list
        5. Segregated list contains only blocks that belong to the size class
3) Heap level:
        1. Prologue/Epilogue blocks are at specific locations (e.g. heap 
           boundaries)and have special size/alloc fields
        2. All blocks stay in between the heap boundaries

Checkheap() does NOT do the following checks:
1) Contiguous free blocks in memory - This is because coalescing is defered 
    in certain situations like extend_heap()
2) Check for invariants - No invariants in this implementation.
*/
int mm_checkheap(int verbose) {
    char *bp = heap_start;
    //Check consistency of prologue header.
    if(GET_SIZE(HDRP(heap_start)) != 8 || !GET_ALLOC(HDRP(heap_start))) {
        printf("Checkheap: Bad prologue header.\n");
        printone(heap_start);
        return(1);
    }

    //Running through the entire heap.
    while(GET_SIZE(HDRP(bp)) != 0) {
        if(verbose) printone(bp);
        
        //Check if block pointer is within memory bounds
        if(!in_heap(bp)) {
            printf("Checkheap: Block out of memory bounds.\n");
            printone(bp);
            return(1);
        }
        //Check if payload area is 8-byte aligned
        if((size_t) bp % 8) {
            printf("Checkheap: Payload area not double-word aligned.\n");
            printone(bp);
            return(1);
        }
        //Check if any free blocks are not in the free list
        if(!GET_ALLOC(HDRP(bp)) && \
            (!in_heap(GET_NEXT_FREE(bp)) || !in_heap(GET_PREV_FREE(bp)))) {
            printf("Checkheap: Free block not in the free list.\n");
            printone(bp);
            return 1;
        }
        bp = NEXT_BLKP(bp);
    }

    //Check consistency of epilogue header
    if(GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
        printf("Checkheap: Bad epilogue header.\n");
        printone(bp);
        return(1);
    }

    //Running through the entire free list.
    for(int i=0;i<BUCKETS;i++) {
        char *ptr = seg_list[i];
        while(ptr != NULL) {
            //Check if headers and footers match
            if(GET(HDRP(bp)) != GET(FTRP(bp))) {
                printf("Checkheap: Header and footer mismatch.\n");
                printone(bp);
                return(1);
            }
            //Check if previous and next free pointers are within memory bounds
            if(!in_heap(GET_PREV_FREE(ptr)) && !in_heap(GET_NEXT_FREE(ptr))) {
                printf("Checkheap: Free pointers out of memory bounds\n");
                return(1);
            }
            //Check if there are any allocated blocks in the free list
            if(!GET_ALLOC(HDRP(ptr))) {
                printf("Checkheap: Free block is not marked as 'free'\n");
                return(1);
            }
            //Check if there are blocks whose sizes don't belong to their class
            //in segregated list
            int size = GET_SIZE(HDRP(bp));
            if(size > (1<<i) || size <= (1<<(i-1))) {
                printf("Checkheap: Block size mismatch in wrong bucket\n");
                return(1);
            }
            ptr = GET_NEXT_FREE(ptr);
        }
    }

    //Check for cycles in free lists (using hare and tortoise algorithm)
    for(int i=0;i<BUCKETS;i++) {
        char *hare = GET_NEXT_FREE(seg_list[i]);
        char *tortoise = seg_list[i];
        while(tortoise != NULL) {
            if(tortoise == hare) {
                printf("Found cycles in the free list\n");
                return(1);
            }
            if(hare != NULL) hare = GET_NEXT_FREE(GET_NEXT_FREE(hare));
            tortoise = GET_NEXT_FREE(tortoise);
        }
    }

    return 0;
}

//Hand-crafted with love by Sandeep Rao
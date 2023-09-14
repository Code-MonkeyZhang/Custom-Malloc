/*
 * mm.c
 *
 * Name: Yufeng Zhang
 *
 * - Segregated free list (9 roots, see pick_root() for details)
 * - First fit
 * - Coalescing
 * - Splitting
 * - Sorted free list
 * 
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */

// #define DEBUG
// #ifdef DEBUG
// // When debugging is enabled, the underlying functions get called
// #define dbg_printf(...) printf(__VA_ARGS__)
// #define dbg_assert(...) assert(__VA_ARGS__)
// #else
// // When debugging is disabled, no code gets generated
// #define dbg_printf(...)
// #define dbg_assert(...)
// #endif // DEBUG

// do not change the following!
#ifdef DRIVER
// create aliases for driver tests
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mm_memset
#define memcpy mm_memcpy
#endif // DRIVER

#define ALIGNMENT 16

// Basic constants
#define WSIZE 8  // Word and header/footer size (bytes)
#define DSIZE 16 // Double word size (bytes)

static char *heap_listp; // Pointer to beginning of heap
static char *root_array[9] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

// List of SMALL helper functions
static size_t align(size_t x);                // rounds up to the nearest multiple of ALIGNMENT
static void put(void *p, uint64_t val);       // write val to p
static uint64_t pack(size_t size, int alloc); // pack size & alloc bit into a word
static char *get_header(void *bp);            // given ptr of user space, get ptr of header
static size_t get_size(void *bp);             // given ptr of header|footer, read the size of entire block
static char *get_footer(void *bp);            // given ptr of user space, get ptr of footer
static char *get_nextblk(void *bp);           // given ptr of user space, get ptr of next block's user space
static char *get_prevblk(void *bp);           // given ptr of user space, get ptr of prev block's user space
static bool get_alloc(void *ptr);             // given ptr of header or footer, get alloc bit
static void set_ptr(void *p, char *val);      // given ptr of a word, set prev|next ptr
static char *get_ptr(void *bp);               // given ptr of a word, read prev|next ptr
static void set_prevalloc(void *p);           // given ptr of header or footer, set prev alloc bit

// List of BIG helper functions
static void *coalesce(char *bp);                           // coalesce helper function
static void *extend_heap(size_t words);                    // extend heap helper function
static void *find_free_list(size_t require_size);          // find free block in free lists
static void allocate(char *bp, size_t size);               // allocate helper function
static void insert_free(char *new_bp, size_t insert_size); // insert free block into free list
static void reset_free(char *bp);                          // reset free block in free list
static int pick_root(size_t size);                         // pick root for insert_free

// List of mm functions
bool mm_init(void);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *oldptr, size_t size);

static size_t align(size_t x)
{
    return ALIGNMENT * ((x + ALIGNMENT - 1) / ALIGNMENT);
}

static void put(void *p, uint64_t val)
{
    *(uint64_t *)p = val;
}

static uint64_t pack(size_t size, int alloc)
{
    return (size) | (alloc);
}

static char *get_header(void *bp)
{
    return (char *)bp - WSIZE;
}

static size_t get_size(void *bp)
{
    // Given ptr of header|footer, read the size of userspace, including header and footer
    //&~0xf to get rid of last 4 bits
    return (size_t)(*(uint64_t *)bp & ~0xf);
}

static char *get_footer(void *bp)
{
    // Given ptr of user space, get ptr of footer
    size_t curr_size = get_size(get_header(bp)); // read size of user space
    return (char *)bp + curr_size - DSIZE;       // bp+size-WSIZE go to footer
}

static char *get_nextblk(void *bp)
{
    // Given ptr of user space, get ptr of next block
    size_t curr_size = get_size(get_header(bp)); // read size of user space+ header&footer
    return (char *)bp + curr_size;               // bp+size go to next block
}

static char *get_prevblk(void *bp)
{
    size_t prev_size = get_size((char *)bp - DSIZE); // go to prev footer and read size
    return (char *)bp - prev_size;
}

static bool get_alloc(void *ptr)
{
    return (bool)(*(unsigned int *)(ptr)&0x1);
}

static void set_ptr(void *p, char *val)
{
    *(uint64_t *)p = (uint64_t)val;
}

static char *get_ptr(void *bp)
{
    return (char *)(*(uint64_t *)bp);
}

static void set_prevalloc(void *p)
{
    *(uint64_t *)p = *(uint64_t *)p | 2;
}

static bool get_prevalloc(void *ptr)
{
    return (bool)(*(unsigned int *)(ptr)&0x2);
}

static int pick_root(size_t size)
{
    // pick root based on size
    int root_index = 8;
    if (size <= 32)
    {
        root_index = 0;
    }
    else if (size <= 64)
    {
        root_index = 1;
    }
    else if (size <= 128)
    {
        root_index = 2;
    }
    else if (size <= 256)
    {
        root_index = 3;
    }
    else if (size <= 512)
    {
        root_index = 4;
    }
    else if (size <= 1024)
    {
        root_index = 5;
    }
    else if (size <= 8192)
    {
        root_index = 6;
    }
    else if (size <= 16384)
    {
        root_index = 7;
    }
    else if (size >= 16384)
    {
        root_index = 8;
    }
    // printf("pick root index: %d for size %zu\n", root_index, size);
    return root_index;
    // return 8;
}

static void *coalesce(char *bp)
{

    bool prev_alloc = get_alloc(bp - DSIZE);
    bool next_alloc = get_alloc(get_header(get_nextblk(bp)));

    // printf("attempt to coalesce %p\n", bp);

    if (!prev_alloc && next_alloc) // case 1: prev free, next allocated
    {

        char *prev_blk = get_prevblk(bp);
        size_t curr_size = get_size(get_header(bp));
        size_t prev_size = get_size(get_header(prev_blk));
        size_t coalesce_size = curr_size + prev_size;

        // delete prev block from free list, before header & footer are updated
        reset_free(prev_blk);

        // update prev header
        put(get_header(prev_blk), pack(coalesce_size, 0));
        // update curr footer
        put(get_footer(bp), pack(coalesce_size, 0));

        // printf("case 1: prev free, next allocated\n");
        // printf("curr_blk: %p, size: %zu\n", bp, curr_size);
        // printf("prev_blk: %p, size: %zu\n", prev_blk, prev_size);

        return prev_blk;
    }
    else if (prev_alloc && !next_alloc) // case 2: prev allocated, next free
    {
        char *next_blk = get_nextblk(bp);
        size_t curr_size = get_size(get_header(bp));
        size_t next_size = get_size(get_header(next_blk));
        size_t coalesce_size = curr_size + next_size;

        // delete next block from free list, before header & footer are updated
        reset_free(next_blk);

        // update curr header
        put(get_header(bp), pack(coalesce_size, 0));
        // update next footer
        put(get_footer(next_blk), pack(coalesce_size, 0));

        // printf("case 2: prev allocated, next free\n");
        // printf("curr_blk: %p, size: %zu\n", bp, curr_size);
        // printf("next_blk: %p, size: %zu\n", next_blk, next_size);

        return bp;
    }
    else if (!prev_alloc && !next_alloc) // case 3: prev free, next free
    {

        char *prev_blk = get_prevblk(bp);
        char *next_blk = get_nextblk(bp);
        size_t curr_size = get_size(get_header(bp));
        size_t prev_size = get_size(get_header(prev_blk));
        size_t next_size = get_size(get_header(next_blk));
        size_t coalesce_size = curr_size + prev_size + next_size;

        // reset free list ptr for prev & next block
        reset_free(prev_blk);
        reset_free(next_blk);

        // update prev header
        put(get_header(prev_blk), pack(coalesce_size, 0));
        // update next footer
        put(get_footer(next_blk), pack(coalesce_size, 0));

        // printf("case3 : prev free, next free\n");
        // printf("curr_blk: %p, size: %zu\n", bp, curr_size);
        // printf("prev_blk: %p, size: %zu\n", prev_blk, prev_size);
        // printf("next_blk: %p, size: %zu\n", next_blk, next_size);

        return prev_blk;
    }

    // printf("case 4: Both prev & next allocated\n");
    return bp; // case 4: prev allocated, next allocated
}

// insert free block into free list
static void insert_free(char *new_bp, size_t insert_size)
{
    // choose root
    int root_index = pick_root(insert_size);
    char *insert_root = root_array[root_index];

    // store ptr in the root
    char *old_prev = get_ptr(insert_root);

    // if root is empty, insert new block as first free block
    if (old_prev == NULL)
    {
        set_ptr(insert_root, new_bp);
        set_ptr(new_bp, insert_root);
        set_ptr(new_bp + WSIZE, NULL);
        return;

        // printf("insert free block %p success\n", new_bp);
        // printf("insert in Root: %d\n", root_index);
    }
    else
    {
        size_t new_size = get_size(get_header(new_bp));
        size_t old_size = get_size(get_header(old_prev));

        // sort free block by size each insertion
        // if new block is smaller than old block, insert before old block
        if (new_size <= old_size)
        {
            set_ptr(insert_root, new_bp);      // set root points to new_bp
            set_ptr(new_bp, insert_root);      // set new_bp prev points to root
            set_ptr(new_bp + WSIZE, old_prev); // set new_bp next points to old_prev
            set_ptr(old_prev, new_bp);         // set old_prev points to new_bp
        }
        // if new block is bigger than old block, insert after old block
        else if (new_size > old_size)
        {
            char *old_next = old_prev + WSIZE;
            char *old_nextblk = get_ptr(old_next);

            set_ptr(old_next, new_bp); // set old_next prev points to new_bp
            set_ptr(new_bp, old_prev); // set new_bp prev points to old_next
            set_ptr(new_bp + WSIZE, NULL);

            if (old_nextblk != NULL)
            {
                set_ptr(new_bp + WSIZE, old_nextblk); // set new_bp next points to old_next
                set_ptr(old_nextblk, new_bp);         // set old_next points to new_bp
            }
        }
        // printf("insert free block %p success\n", new_bp);
    }
}

static void reset_free(char *bp)
{
    char *prev = get_ptr(bp);
    char *next = get_ptr(bp + WSIZE);

    // identify root
    int root_index = pick_root(get_size(get_header(bp)));
    char *root = root_array[root_index];

    if (prev == 0 && next == 0)
    {
        return;
    }

    if (prev == root) // if node is first in list
    {
        if (next) // if node has next
        {
            set_ptr(next, root); // set prevptr of next blocl points to root
        }
        set_ptr(root, next); // set root points to next block
    }

    else // if node is in the middle of list
    {
        if (next) // if node has next
        {
            set_ptr(next, prev); // set prevptr of next block points to prev block
        }
        set_ptr(prev + WSIZE, next); // set nextptr of prev block points to next block
    }
}

// extend heap helper function
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size = align(words); // align size
    // make sure size is at least 4 words
    if (size < WSIZE * 4)
    {
        size = WSIZE * 4;
    }

    if ((bp = mm_sbrk(size)) == (void *)-1)
    {
        return NULL;
    }

    /* Initialize free block header/footer & update the epilogue header */
    put(get_header(bp), pack(size, 0)); /* Free block header */
    set_ptr(bp, NULL);                  /* Free block prev ptr */
    set_ptr(bp + WSIZE, NULL);          /* Free block next ptr */

    put(get_footer(bp), pack(size, 0)); /* Free block footer */

    put(get_header(get_nextblk(bp)), pack(0, 3)); /* New epilogue header */

    // printf("extend heap by %zu success\n", words);

    mm_checkheap(__LINE__);
    return bp;
}

static void *find_free_list(size_t require_size)
{

    // pick root based on size
    int root_index = pick_root(require_size);
    char *root;
    char *iter;
    // iterate from root_index to 8
    // printf("attempt to find freeblk for size %zu from root %d\n", require_size, root_index);
    for (int i = root_index; i < 9; i++)
    {
        root = root_array[i];
        iter = get_ptr(root);
        // iterating from root
        while (iter != NULL)
        {
            if (get_size(get_header(iter)) >= require_size)
            {
                return iter;
            }
            iter = get_ptr(iter + WSIZE);
        }
    }

    // printf("no free list found\n");
    return NULL; // No fit
}

// helper function
// given ptr of free block & required size
// if free block has at least 4*WSIZE extra space after allocated, split
// if not, allocate the whole block
static void allocate(char *bp, size_t requested_size)
{
    // get total size of free block
    size_t total_size = get_size(get_header(bp));
    size_t remain_size = total_size - requested_size - DSIZE;
    size_t allocate_size = requested_size + DSIZE;

    // if free block is big enough, split
    if (remain_size >= WSIZE * 4)
    {
        reset_free(bp); // reset prev&next ptr in the block
        // update size & alloc bit of header & footer in allocated block
        put(get_header(bp), pack(allocate_size, 1));
        put(bp + allocate_size - DSIZE, pack(allocate_size, 1));

        char *remainblk = bp + allocate_size;

        // update size & alloc bit of header & footer in remaining block
        put(get_header(remainblk), pack(remain_size, 0));
        put(get_footer(remainblk), pack(remain_size, 0));
        set_ptr(remainblk, NULL); // reset prev&next ptr in the block
        set_ptr(remainblk + WSIZE, NULL);

        // printf("split %p success! \ntotal_size: %zu\nallocate_size: %zu\nremain_size: %zu \nnew free blk: %p\n", (void *)bp, total_size, allocate_size, remain_size, remainblk);

        // coalesce & insert remain free block into free list
        char *coalece_block = coalesce(remainblk);
        insert_free(coalece_block, get_size(get_header(coalece_block)));
    }
    else
    {
        reset_free(bp); // reset prev&next ptr in the block
        // if not, allocate the whole block
        put(get_header(bp), pack(total_size, 1));
        put(get_footer(bp), pack(total_size, 1));
    }
}

/*
 * mm_init: returns false on error, true on success.
 */

bool mm_init(void)
{
    // Create an empty heap
    if ((heap_listp = mm_sbrk(12 * WSIZE)) == (void *)-1)
    {
        return false;
    }
    // initialize free list root array
    for (int i = 0; i < 9; i++)
    {
        root_array[i] = heap_listp + i * WSIZE;
        put(root_array[i], 0);
    }

    // Initialize heap space
    put(heap_listp + 9 * WSIZE, 0);               // Alignment block
    put(heap_listp + 10 * WSIZE, pack(DSIZE, 1)); // Prologue header
    put(heap_listp + 11 * WSIZE, pack(DSIZE, 1)); // Prologue footer
    put(heap_listp + 12 * WSIZE, pack(0, 3));     // Epilogue header

    // Extend the empty heap by 512 bytes
    char *bp = extend_heap(512);
    if (bp == NULL)
    {
        return false;
    }

    // choose root & insert free block into free list
    insert_free(bp, 512);
    // printf("############################### initialize success ##################################### \n");
    return true;
}

/*
 * malloc
 */

void *malloc(size_t size)
{
    // invalid request
    if (size <= 0)
    {
        return NULL;
    }

    char *bp;
    size_t align_side = align(size); // align size

    // search free list for a fit
    bp = find_free_list(align_side + DSIZE);
    if (bp != NULL)
    {
        allocate(bp, align_side);
        // printf("malloc success! addr: %p, size: %zu\n\n", (void *)bp, size);
        mm_checkheap(__LINE__);
        return bp;
    }

    // no fit found, extend heap
    size_t total_size = align_side + DSIZE;
    bp = extend_heap(total_size); // add DSIZE for header & footer
    if (bp == NULL)
    {
        return NULL;
    }
    // allocate(bp, align_side);
    put(get_header(bp), pack(total_size, 1));
    put(get_footer(bp), pack(total_size, 1));

    // printf("malloc success! addr: %p, size: %zu\n\n", (void *)bp, size);

    mm_checkheap(__LINE__);
    return bp;
}

/*
 * free
 */
void free(void *ptr)
{
    // invalid request
    if (ptr == NULL)
    {
        return;
    }

    char *curr_header = get_header(ptr);
    char *next_blk = get_nextblk(ptr);

    // check if ptr is allocated
    if (!get_alloc(curr_header))
    {
        return;
    }

    size_t block_size = get_size(curr_header);

    // printf("attempt to free %p, size: %zu\n", ptr, block_size);

    // update size & alloc bit of header & footer
    put(curr_header, pack(block_size, 0));
    put(get_footer(ptr), pack(block_size, 0));

    // each time free, set next blk's prevalloc bit to 1
    set_prevalloc(next_blk - 8);

    // clear out prev & next block
    set_ptr(ptr, NULL);
    set_ptr(ptr + WSIZE, NULL);

    // coalesce & insert free block into free list
    char *coalece_block = coalesce(ptr);
    insert_free(coalece_block, get_size(get_header(coalece_block)));
}

/*
 * realloc
 */
void *realloc(void *oldptr, size_t size)
{
    if (size == 0)
    {
        free(oldptr);
        return NULL;
    }

    if (oldptr == NULL)
    {
        oldptr = malloc(size);
        return oldptr;
    }

    size_t oldsize = get_size(get_header(oldptr));
    if (oldsize == size + DSIZE)
    {
        // printf("realloc same size, return oldptr\n");
        return oldptr;
    }

    // general case
    // allocate a new block
    void *newptr = malloc(size);
    if (newptr == NULL)
    {
        return NULL;
    }

    // copy data from old block to new block
    // if new block is smaller than old block, copy small size bytes
    if (size < oldsize - DSIZE)
    {
        mm_memcpy(newptr, oldptr, size);
    }
    else if (size > oldsize - DSIZE)
    {
        mm_memcpy(newptr, oldptr, oldsize);
    }

    free(oldptr);
    return newptr;
}
/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void *calloc(size_t nmemb, size_t size)
{
    void *ptr;
    size *= nmemb;

    ptr = malloc(size);
    if (ptr)
    {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void *p)
{
    return p <= mm_heap_hi() && p >= mm_heap_lo();
}

/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void *p)
{
    size_t ip = (size_t)p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 * You call the function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 * function where there was an invalid heap.
 */

// is_epilogue: return whether the block is epilogue
static inline bool is_epilogue(char *bp)
{
    return (get_size(get_header(bp)) == 0) && (get_alloc(get_header(bp)) == 1);
}

bool mm_checkheap(int line_number)
{
#ifdef DEBUG
    // Write code to check heap invariants here
    // IMPLEMENT THIS
    for (int i = 0; i < 9; i++)
    {
        for (char *curr = get_ptr(root_array[i]); in_heap(curr) && !is_epilogue(curr); curr = get_ptr(curr + WSIZE))
        {

            // check header & footer size consistency
            size_t head_size = get_size(get_header(curr));
            size_t foot_size = get_size(get_footer(curr));
            if (head_size != foot_size)
            {
                printf("Warning: header & footer size inconsistent! \n line: %d\n addr: %p\n Head Size: %zu\n Foot Size: %zu\n\n", line_number, curr, head_size, foot_size);
                return false;
            }
            // check header & footer alloc bit consistency
            if (get_alloc(get_header(curr)) != get_alloc(get_footer(curr)))
            {
                printf("Warning: header & footer alloc bit inconsistent at line %d\n", line_number);
                return false;
            }
            // check if in heap
            if (!in_heap(get_nextblk(curr) - 1))
            {
                printf("Warning: next block out of heap at line %d\n", line_number);
                return false;
            }
            // check size
            size_t size = get_size(get_header(curr));
            if (pick_root(size) != i)
            {
                printf("Warning: size of current block doesn't mactch its lists %d\n size: %zu root_index:%d\n", line_number, size, i);
                return false;
            }
        }
    }
#endif // DEBUG
    return true;
}

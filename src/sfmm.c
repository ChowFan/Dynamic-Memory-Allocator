/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
#define MINSIZE 32

int fib[7] = {1, 2, 3, 5, 8, 13, 21};

// Initializes the heap
int init_heap() {
    if (!sf_mem_grow()) {
        return 1;
    }
    // Create prologue block
    void *prologuep = sf_mem_start() + 0x18;
    sf_block prologue;
    prologue.header = 0x20 | THIS_BLOCK_ALLOCATED;
    (*(sf_block *)prologuep) = prologue;
    // Footer of prologue block
    ((*(sf_footer *)(prologuep + 0x18))) = prologue.header;

    // Initialize free block
    void *freeblkp = sf_mem_start() + 0x18 + 0x20;
    sf_block freeblk;
    freeblk.header = sf_mem_end() - sf_mem_start() - 32 - 8 - 24;
    (*(sf_block *)freeblkp) = freeblk;
    // Footer of free block
    ((*(sf_footer *)(freeblkp + freeblk.header - 8))) = freeblk.header;

    // Create epilogue block
    void *epiloguep = sf_mem_end() - 8;
    sf_block epilogue;
    epilogue.header = 0 | THIS_BLOCK_ALLOCATED;
    (*(sf_block *)epiloguep) = epilogue;

    //Initialize free list
    for (int i=0; i < NUM_FREE_LISTS - 1; i++) {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
    // Insert free block as wilderness block
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = freeblkp;
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = freeblkp;
    (*(sf_block *)freeblkp).body.links.next = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    (*(sf_block *)freeblkp).body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    return 0;
}

// Coalesces the given block with the prev and next blocks
// TODO: Better checking for out of heap
void *coalesce_block(void *pp) {
    sf_block *orig_blockp = pp;
    size_t current_block_size = ((sf_block *)pp)->header & ~0x10;
    // Check if prev block is free + coalesce
    if (!((*(sf_footer *)(pp - 8)) & THIS_BLOCK_ALLOCATED) && (pp - 32 >= sf_mem_start())) {
        size_t prev_block_size = (*(sf_footer *)(pp - 8)) & ~0x10;
        pp -= prev_block_size;
        // Remove from free list + update next/prev pointers
        ((sf_block *)pp)->body.links.next->body.links.prev = ((sf_block *)pp)->body.links.prev;
        ((sf_block *)pp)->body.links.prev->body.links.next = ((sf_block *)pp)->body.links.next;

        current_block_size += prev_block_size;
        // Update header and footer of coalesced block
        ((sf_block *)pp)->header = current_block_size;
        (*(sf_footer *)(pp + current_block_size - 8)) = current_block_size;
    }
    // Check if next block is free + coalesce
    if (!((*(unsigned int *)(pp + current_block_size)) & THIS_BLOCK_ALLOCATED) && (pp + current_block_size + 32) <= sf_mem_end()) {
        sf_block *next_block = pp + current_block_size;
        size_t next_block_size = next_block->header & ~0x10;
        // Remove from free list + update next/prev pointers
        next_block->body.links.next->body.links.prev = next_block->body.links.prev;
        next_block->body.links.prev->body.links.next = next_block->body.links.next;

        current_block_size += next_block_size;
        // Update header and footer of coalesced block
        ((sf_block *)pp)->header = current_block_size;
        (*(sf_footer *)(pp + current_block_size - 8)) = current_block_size;
    }
    // Update old free list
    if (orig_blockp->body.links.next >= (sf_block *)sf_mem_start() && orig_blockp->body.links.next <= (sf_block *)sf_mem_end() &&
        orig_blockp->body.links.prev >= (sf_block *)sf_mem_start() && orig_blockp->body.links.prev <= (sf_block *)sf_mem_end()) {
        orig_blockp->body.links.next->body.links.prev = orig_blockp->body.links.prev;
        orig_blockp->body.links.prev->body.links.next = orig_blockp->body.links.next;
    }
    // Add new coalesced free block to new free list
    sf_block *free_list_head;
    for (int i=0; i < 7; i++) {
        if (i == 6) {
            free_list_head = &sf_free_list_heads[i];
            break;
        }
        if (current_block_size <= MINSIZE * fib[i]) {
            free_list_head = &sf_free_list_heads[i];
            break;
        }
    }
    // Check if the newly coalesced free block is a wilderness block
    if (!(*(sf_footer *)(pp + current_block_size) & ~0x10) && (*(sf_footer *)(pp + current_block_size) & THIS_BLOCK_ALLOCATED)) {
        free_list_head = &sf_free_list_heads[7];
    }
    free_list_head->body.links.next->body.links.prev = pp;
    ((sf_block *)pp)->body.links.next = free_list_head->body.links.next;
    ((sf_block *)pp)->body.links.prev = free_list_head;
    free_list_head->body.links.next = pp;
    return pp;
}

// Extends the heap and properly coalesces. Returns 0 on success.
int extend_heap() {
    void *old_mem_end = sf_mem_grow();
    // Check if NULL
    if (!old_mem_end) {
        return 1;
    }
    // Old epilogue --> new header of free block
    sf_block *epiloguep = old_mem_end - 8;
    void *new_block = epiloguep;
    size_t size = sf_mem_end() - old_mem_end;
    epiloguep->header = size;
    // Create new epilogue
    epiloguep = sf_mem_end() - 8;
    epiloguep->header = 0 | THIS_BLOCK_ALLOCATED;
    // Footer of new free block
    (*(sf_footer *)(new_block + (size - 8))) = ((sf_block *)new_block)->header;
    coalesce_block(new_block);
    return 0;
}

// Splits block *pp into two blocks
void split_block(void *pp, size_t size) {
    size_t total_size = ((sf_block *)pp)->header & ~0x10;
    size_t new_blk_size = total_size - size;
    if (new_blk_size == 0) {
        return;
    }
    // Update header and create footer for pp (allocated)
    ((sf_block *)pp)->header = size | THIS_BLOCK_ALLOCATED;
    (*(sf_footer *)(pp + size - 8)) = size | THIS_BLOCK_ALLOCATED;
    // Create header and update footer for new split block (free)
    void *pp_new = pp + size;
    ((sf_block *)pp_new)->header = new_blk_size;
    *(sf_footer *)(pp_new + new_blk_size - 8) = new_blk_size;
    // Coalesce the free block
    coalesce_block(pp_new);
}

void *sf_malloc(size_t size) {
    if (size <= 0) {
        return NULL;
    }
    if (sf_mem_start() == sf_mem_end()) {
        if (init_heap()) {
            sf_errno = ENOMEM;
            return NULL;
        }
    }
    // Find which size class the malloc request belongs to
    int size_class = 0;
    size_t block_size = size + 8 + 8; // Payload + header + footer
    size_t free_block_size = 0;
    if (block_size % MINSIZE) {
        block_size += MINSIZE - (block_size % MINSIZE);
    }
    sf_block *free_list_head;
    sf_block *blockp;
    for (int i=0; i < 7; i++) {
        size_class = i;
        if (i == 6) {
            free_list_head = &sf_free_list_heads[i];
            break;
        }
        if (block_size <= MINSIZE * fib[i]) {
            free_list_head = &sf_free_list_heads[i];
            break;
        }
    }
    // Loop through free list to find first block of sufficiently large size
    sf_block *sentinelp = free_list_head;
    do {
        blockp = sentinelp->body.links.next;
        while (blockp != sentinelp) {
            if (block_size <= blockp->header) {
                break;
            } else {
                blockp = blockp->body.links.next;
            }
        }
        if (blockp == sentinelp)  {
            if (size_class == 7) {
                // Extend heap if wilderness block isn't big enough
                if (extend_heap()) {
                    sf_errno = ENOMEM;
                    return NULL;
                }
            } else {
                // Use next size class of free list
                free_list_head = &sf_free_list_heads[size_class + 1];
                size_class++;
                sentinelp = free_list_head;
                blockp = free_list_head;
            }
        } else {
            free_block_size = blockp->header;
            blockp->header |= THIS_BLOCK_ALLOCATED;
            (*(sf_footer *)((void *)blockp + free_block_size - 8)) = blockp->header;
        }
    } while (blockp == sentinelp);
    // Remove free block from free list
    blockp->body.links.next->body.links.prev = blockp->body.links.prev;
    blockp->body.links.prev->body.links.next = blockp->body.links.next;

    // Split free block if too large
    if (free_block_size - block_size >= MINSIZE) {
        split_block(blockp, block_size);
    }
    return ((void *)blockp + 8);
}

// Checks if a pointer is valid
int is_valid_pointer(void *pp) {
    // Check if the pointer is NULL and if is 32-byte aligned
    if (!pp || ((unsigned long)pp & 0x1f) != 0) {
        return 0;
    }
    sf_header *pp_h = &((sf_block *)(pp - 8))->header;
    size_t size = *pp_h & ~0x10;
    // Check if the block size < 32 and is not a multiple of 32
    if (size < 32 || size % 32) {
        return 0;
    }
    // Check if the allocated bit is set to 0
    if (!(*pp_h & 0x10)) {
        return 0;
    }
    // Check if header is before the start of the first block of the heap
    if ((unsigned long)(pp - 8) < (unsigned long)(sf_mem_start() + 56)) {
        return 0;
    }
    // Check if footer is after the end of the last block of the heap
    if ((unsigned long)(pp + size) > (unsigned long)(sf_mem_end() - 16)) {
        return 0;
    }
    return 1;
}

void sf_free(void *pp) {
    // Check if pointer is valid
    if (!is_valid_pointer(pp)) {
        abort();
    }
    sf_header *pp_h = &((sf_block *)(pp - 8))->header;
    size_t size = *pp_h & ~0x10;
    // Pointer is valid, free the block
    // Set allocated bit to 0 in header and footer
    *pp_h = size;
    *(sf_footer *)(pp - 8 + size - 8) = size;
    // Coalesce and insert into free list
    coalesce_block(pp - 8);
    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    // Check if pointer is valid
    if (!is_valid_pointer(pp)) {
        sf_errno = EINVAL;
        return NULL;
    }
    // Pointer is valid, check rsize
    if (rsize == 0) {
        sf_free(pp);
        return NULL;
    }
    // Case: reallocating to the same block size
    size_t block_size = (((sf_block *)(pp - 8))->header) & ~0x10;
    size_t payload_size = block_size - 16;
    size_t new_block_size = rsize + 16; // + header + footer
    if (new_block_size % MINSIZE) {
        new_block_size += MINSIZE - (new_block_size % MINSIZE);
    }
    if (new_block_size == block_size) {
        return pp;
    }
    // Case: reallocating to a larger block size
    if (new_block_size > block_size) {
        void *new_block = sf_malloc(rsize);
        if (new_block == NULL) {
            return NULL;
        }
        memcpy(new_block, pp, payload_size);
        sf_free(pp);
        return new_block;
    }
    // Case: reallocating to a smaller block size
    if (new_block_size < block_size) {
        // Check if splitting will cause splinters
        if (block_size - new_block_size < MINSIZE) {
            return pp;
        } else {
            split_block(pp - 8, new_block_size);
            return pp;
        }
    }
    return NULL;
}

// Returns 1 if num is a power of 2 and returns 0 otherwise
int is_power_of_two(size_t num) {
    if (num <= 0) {
        return 0;
    }
    while (num > 1) {
        if (num % 2) {
            return 0;
        }
        num >>= 1;
    }
    return 1;
}

void *sf_memalign(size_t size, size_t align) {
    // If align < MINSIZE OR if align is not a power of 2 set sf_errno and return NULL
    if (align < MINSIZE || !is_power_of_two(align)) {
        sf_errno = EINVAL;
        return NULL;
    }
    if (size == 0) {
        return NULL;
    }
    size_t requested_block_size = size + align + MINSIZE + 16;
    void *block = sf_malloc(requested_block_size);
    if (block == NULL) {
        return NULL;
    }
    size_t block_size = (((sf_block *)(block - 8))->header) & ~0x10;
    // Check if block satisfies the alignment
    if ((unsigned long)block % align == 0) {
        // Split block if too large
        size_t effective_size = size;
        if (effective_size % MINSIZE) {
            effective_size += MINSIZE - (effective_size % MINSIZE);
        }
        split_block(block - 8, effective_size);
        return block;
    }
    unsigned long target_addr = (unsigned long)block;
    target_addr += align - (target_addr % align);
    // Split initial portion of block if necessary
    if (target_addr - (unsigned long)block >= MINSIZE) {
        void *split_blockp = block - 8;
        size_t split_block_size = target_addr - (unsigned long)block;
        // Update header and footer of the split block
        ((sf_block *)split_blockp)->header = split_block_size;
        *((sf_footer *)(split_blockp + split_block_size - 8)) = split_block_size;
        // Update header and footer of the allocated block
        block = split_blockp + split_block_size;
        block_size -= split_block_size;
        ((sf_block *)block)->header = block_size | THIS_BLOCK_ALLOCATED;
        *((sf_footer *)(block + block_size - 8)) = block_size | THIS_BLOCK_ALLOCATED;
        // Coalesce the split free block
        coalesce_block(split_blockp);
    }
    // Split if block is still too large
    size_t effective_size = size;
    if (effective_size % MINSIZE) {
        effective_size += MINSIZE - (effective_size % MINSIZE);
    }
    split_block(block, effective_size);
    return block + 8;
}

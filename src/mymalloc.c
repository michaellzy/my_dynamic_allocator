#include "mymalloc.h"


// Word alignment
const size_t kAlignment = sizeof(size_t);
// Minimum allocation size (1 word)
const size_t kMinAllocationSize = kAlignment;
// Size of meta-data per Block
const size_t kMetadataSize = sizeof(Block);
// Maximum allocation size (128 MB)
const size_t kMaxAllocationSize = (128ull << 20) - kMetadataSize;
// Memory size that is mmapped (64 MB)
const size_t kMemorySize = (64ull << 20);

const size_t kMaxAvailableSize = kMaxAllocationSize - kMetadataSize; 

Block *fencepost_start = NULL, *fencepost_end = NULL;

inline static size_t round_up(size_t size, size_t alignment) {
  const size_t mask = alignment - 1;
  return (size + mask) & ~mask;
}


void initialize_memory() {
  fencepost_start = mmap(NULL, kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (fencepost_start == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    return NULL;
  }
  fencepost_start->allocated = 1;
  fencepost_start->prev = NULL;
  fencepost_start->size = 0;

  Block *start_block = ADD_BYTES(fencepost_start, kMetadataSize);
  start_block->allocated = 0;
  start_block->size = kMemorySize - 2 * kMetadataSize;
  start_block->prev = fencepost_start;
  fencepost_start->next = start_block;

  fencepost_end = ADD_BYTES(start_block, start_block->size);
  fencepost_end->size = 0;
  fencepost_end->allocated = 1;
  fencepost_end->next = NULL;
  fencepost_end->prev = start_block;
  start_block->next = fencepost_end;
}

Block* find_free_block(size_t size) {
  Block *start, *best;
  start = get_start_block();
  size_t best_fit = __SIZE_MAX__;
  while (start != fencepost_end) {
    if (is_free(start) && block_size(start) >= size) {
      if (block_size(start) < best_fit) {
        best_fit = block_size(start);
        best = start;
      }
    }
    start = start->next;
  }
  return best;
}

void *my_malloc(size_t size) {
  if (fencepost_start == NULL) {
    initialize_memory();
  }
  size_t alloc_size = round_up(size + kMetadataSize, kAlignment);
  Block *free_block = find_free_block(alloc_size);
  if (free_block == NULL) {
    return NULL;
  } else {
    free_block->allocated = 1;
    free_block->size = alloc_size;
    Block *prev = free_block->prev;
    Block *next = free_block->next;

  }

  return NULL;
}

void my_free(void *ptr) {
  return;
}

/** These are helper functions you are required to implement for internal testing
 *  purposes. Depending on the optimisations you implement, you will need to
 *  update these functions yourself.
 **/

/* Returns 1 if the given block is free, 0 if not. */
int is_free(Block *block) {
  return !block->allocated;
}

/* Returns the size of the given block */
size_t block_size(Block *block) {
  return block->size;
}

/* Returns the first block in memory (excluding fenceposts) */
Block *get_start_block(void) {
  return fencepost_start->next;
}

/* Returns the next block in memory */
Block *get_next_block(Block *block) {
  if (block == NULL) {
    return NULL;
  }
;
  if (block->next == fencepost_end) {
    return NULL;
  }
  return block->next;

}

/* Given a ptr assumed to be returned from a previous call to `my_malloc`,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr) {
  return ADD_BYTES(ptr, -((size_t) kMetadataSize));
}

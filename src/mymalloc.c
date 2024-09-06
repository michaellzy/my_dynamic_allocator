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

const size_t kAvailableSize = kMemorySize - 2 * kMetadataSize;

Block *fencepost_start = NULL, *fencepost_end = NULL;
Block *free_list_start = NULL;


inline static size_t round_up(size_t size, size_t alignment) {
  const size_t mask = alignment - 1;
  return (size + mask) & ~mask;
}


void initialize_memory() {

  fencepost_start = mmap(NULL, kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (fencepost_start == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    exit(1);
  }
  fencepost_start->allocated = 1;
  fencepost_start->next = NULL;
  fencepost_start->prev = NULL;
  fencepost_start->size = 0;

  free_list_start = ADD_BYTES(fencepost_start, kMetadataSize);
  free_list_start->allocated = 0;
  free_list_start->size = kAvailableSize;
  free_list_start->next = NULL;
  free_list_start->prev = fencepost_start;


  fencepost_end = ADD_BYTES(free_list_start, free_list_start->size);
  fencepost_end->size = 0;
  fencepost_end->allocated = 1;
  fencepost_end->next = NULL;
  fencepost_end->prev = free_list_start;

  fencepost_start->next = free_list_start;
  free_list_start->next = fencepost_end;
}

Block *find_free_block(size_t size) {
  Block *start = fencepost_start->next;
  Block *best = NULL;
  size_t best_fit = kAvailableSize + 1;

  while (start != NULL && start != fencepost_end) {
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

void insert_free_list(Block *block) {
  block->allocated = 0;
  Block *next = block->next;
  fencepost_start->next = block;
  block->prev = fencepost_start;
  block->next = next;
  next->prev = block;
}

void remove_from_free_list(Block *block) {
  if (block != fencepost_start || block != fencepost_end) {
    block->prev->next = block->next;
    block->next->prev = block->prev;
  }
}

Block *split_block(Block *block, size_t size) {
  size_t remain_size = block->size - size - kMetadataSize;
  block->size = remain_size;

  Block* right = get_next_block(block);
  right->size = size + kMetadataSize;
  right->allocated = 1;
  void *payload_ptr = (void*)(right + kMetadataSize);
  memset(payload_ptr, 0, size);
  return block;
}

void *my_malloc(size_t size) {

  if (size == 0 || size > kMaxAllocationSize) {
    return NULL;
  }
  if (fencepost_start == NULL) {
    initialize_memory();
  }
  size_t alloc_size = round_up(size + kMetadataSize, kAlignment);
  Block *free_block = find_free_block(alloc_size);
  if (free_block == NULL) {
    return NULL;
  } else if (free_block->size <= (alloc_size + kMetadataSize + kMinAllocationSize)){
    remove_from_free_list(free_block);
    free_block->allocated = 1;
    free_block->size = size + kMetadataSize;
    void *payload_ptr = (void*)(free_block + kMetadataSize);
    memset(payload_ptr, 0, size);
    return ADD_BYTES(free_block, kMetadataSize);
  }
  Block *splitted_free_block = split_block(free_block, size);
  insert_free_list(splitted_free_block);

  return ADD_BYTES(splitted_free_block, kMetadataSize);
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
  return free_list_start;
}

/* Returns the next block in memory */
Block *get_next_block(Block *block) {
  if (block == NULL) {
    return NULL;
  }
  
  Block* next_block = ADD_BYTES(block, block_size(block));
  if (next_block >= fencepost_end) {
    return NULL;
  }
  return next_block;
}

/* Given a ptr assumed to be returned from a previous call to `my_malloc`,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr) {
  return ADD_BYTES(ptr, -((size_t) kMetadataSize));
}

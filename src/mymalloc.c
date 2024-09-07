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
  // block->allocated = 0;
  Block *next = fencepost_start->next;
  fencepost_start->next = block;
  block->prev = fencepost_start;
  block->next = next;
  next->prev = block;
}

void remove_from_free_list(Block *block) {
  if (block != fencepost_start && block != fencepost_end) {
    block->prev->next = block->next;
    block->next->prev = block->prev;
  }
}



Block *split_block(Block *block, size_t size) {
  size_t remain_size = block->size - size;
  block->size = remain_size;
  block->allocated = 0;
  if (remain_size >= kMetadataSize + kMinAllocationSize) {
    insert_free_list(block);
  } else {
    block->allocated = 1;
  }

  Block* right = get_next_block(block);
  right->size = size;
  right->allocated = 1;
  void *payload_ptr = ADD_BYTES(right, kMetadataSize);
  memset(payload_ptr, 0, size);
  return payload_ptr;
}



// void coalesce_adjacent_blocks(Block *free_block) {
//   Block *cur = fencepost_start->next;

//   while (cur != NULL && cur != fencepost_end) {
//     Block *next_block = get_next_block(cur);

//     // Check if current block is adjacent to free_block
//     if (cur != free_block && is_free(cur)) {
//       // Check if cur is directly before free_block
//       if (ADD_BYTES(cur, block_size(cur)) == free_block) {
//         // Coalesce cur with free_block
//         cur->size += free_block->size;
//         remove_from_free_list(free_block);
//         free_block = cur;
//       }

//       // Check if cur is directly after free_block
//       if (ADD_BYTES(free_block, block_size(free_block)) == cur) {
//         // Coalesce free_block with cur
//         free_block->size += cur->size;
//         remove_from_free_list(cur);
//       }
//     }

//     cur = cur->next;
//   }
// }

void coalesce_adjacent_blocks(Block *free_block) {
    Block *cur = fencepost_start->next;

    // Iterate through the free list
    while (cur != NULL && cur != fencepost_end) {
        Block *next_block = cur->next;
        Block *prev_block = cur->prev;

        // Case 1: Coalesce with the previous block (cur is directly before free_block)
        if (cur != free_block && is_free(cur) && ADD_BYTES(cur, block_size(cur)) == free_block) {
            // Coalesce cur with free_block
            cur->size += free_block->size;
            remove_from_free_list(free_block);  // Remove free_block
            free_block = cur;  // Update free_block to point to the coalesced block at the left position
        }

        // Case 2: Coalesce with the next block (cur is directly after free_block)
        if (cur != free_block && is_free(cur) && ADD_BYTES(free_block, block_size(free_block)) == cur) {
            // Coalesce free_block with cur
            free_block->size += cur->size;
            remove_from_free_list(cur);  // Remove cur from the free list
        }

        // Case 3: Coalesce with both the previous and next blocks
        if (prev_block != NULL && is_free(prev_block) && next_block != NULL && is_free(next_block)) {
            // Coalesce prev_block, free_block, and next_block
            prev_block->size += free_block->size + next_block->size;
            remove_from_free_list(free_block);  // Remove free_block
            remove_from_free_list(next_block);  // Remove next_block
            prev_block->next = next_block->next;  // Adjust pointers
            if (next_block->next != NULL) {
                next_block->next->prev = prev_block;
            }
            free_block = prev_block;  // Coalesced block remains at the left position (prev_block)
        }

        // Move to the next block
        cur = cur->next;
    }
}


void *request_more_memory() {
  // Request more memory using mmap
  Block *new_block = mmap(NULL, kMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (new_block == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    return NULL;
  }

  // Set up the new block
  new_block->size = kMemorySize;
  new_block->allocated = 0;
  new_block->prev = NULL;
  new_block->next = NULL;

  // Insert the new block into the free list
  insert_free_list(new_block);

  // Coalesce the new block with adjacent blocks if possible
  // coalesce_adjacent_blocks(new_block);

  return new_block;
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
    // return NULL;
    // No suitable free block, request more memory from the kernel
    free_block = request_more_memory();
  } 

  remove_from_free_list(free_block);
  
  if (free_block->size <= (alloc_size + kMetadataSize + kMinAllocationSize)){
    // remove_from_free_list(free_block);
    free_block->allocated = 1;
    void *payload_ptr = ADD_BYTES(free_block, kMetadataSize);
    memset(payload_ptr, 0, size);
    return payload_ptr;
  }

  Block *payload = split_block(free_block, alloc_size);
  
  return payload;
}

void my_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  // Convert the payload pointer back to the metadata pointer
  Block *block = ptr_to_block(ptr);

  if (fencepost_start == NULL || fencepost_end == NULL) {
    free(ptr);
    return;
  }

  
  if (block <= fencepost_start || block >= fencepost_end || is_free(block) == 1) {
    return;
  }
  
  block->allocated = 0;  // Mark the block as free
  insert_free_list(block);

  // Coalesce the block with its neighbors if possible
  coalesce_adjacent_blocks(block);
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

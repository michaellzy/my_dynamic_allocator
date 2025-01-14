#include "my_malloc_base.h"

// Word alignment
const size_t kAlignment = sizeof(size_t);
// Minimum allocation size (1 word)
const size_t kMinAllocationSize = kAlignment;
// Size of meta-data per Block
const size_t kMetadataSize = sizeof(Block);
// Maximum allocation size (128 MB)
const size_t kMaxAllocationSize = (128ull << 20) - 4 * kMetadataSize;
// Memory size that is mmapped (64 MB)
const size_t kMemorySize = (64ull << 20);

const size_t kAvailableSize = kMemorySize - 2 * kMetadataSize;

Block *free_list_head = NULL;
Block *free_list_tail = NULL;
Block *cur_free_block = NULL;

static int is_requested_memory = 0;

static struct ChunkInfo chunk_arr[128];
static int chunk_idx = 0;

Block *cur_fencepost_start = NULL, *cur_fencepost_end = NULL;


inline static size_t round_up(size_t size, size_t alignment) {
  const size_t mask = alignment - 1;
  return (size + mask) & ~mask;
}

void initialize() {
  // Allocate memory for the head and tail of the free list using mmap
  free_list_head = (Block *)mmap(NULL, sizeof(Block), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  free_list_tail = (Block *)mmap(NULL, sizeof(Block), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (free_list_head == MAP_FAILED || free_list_tail == MAP_FAILED) {
    fprintf(stderr, "mmap failed to allocate memory for free list head or tail.\n");
    exit(1);
  }

  // Initialize the head and tail of the free list
  free_list_head->size = 0;
  free_list_head->allocated = 1;
  free_list_tail->size = 0;
  free_list_tail->allocated = 1;

  // Set up the linked list
  free_list_head->next = free_list_tail;
  free_list_tail->prev = free_list_head;
}

int get_chunk_size(size_t alloc_size) {
  int n = 1;
  while (n * kAvailableSize < alloc_size) {
    n++;
  }
  return n;
}


struct ChunkInfo request_memory(int n) {
  is_requested_memory = 1;
  struct ChunkInfo c;
  Block *fencepost_start = NULL, *fencepost_end = NULL;
  Block *free_list_start = NULL;
  size_t request_mem_size = n * kMemorySize;
  Block *head = mmap(NULL, request_mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (head == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    exit(1);
  }
  fencepost_start = head;
  fencepost_start->allocated = 1;
  fencepost_start->next = NULL;
  fencepost_start->prev = NULL;
  fencepost_start->size = kMetadataSize;

  free_list_start = ADD_BYTES(fencepost_start, kMetadataSize);
  free_list_start->allocated = 0;
  free_list_start->size = n * kAvailableSize;
  free_list_start->next = NULL;
  free_list_start->prev = NULL;

  fencepost_end = ADD_BYTES(free_list_start, free_list_start->size);
  fencepost_end->size = kMetadataSize;
  fencepost_end->allocated = 1;
  fencepost_end->next = NULL;
  fencepost_end->prev = NULL;

  c.fencepost_start = fencepost_start;
  c.fencepost_end = fencepost_end;
  c.block_start = free_list_start;
  return c;
}

struct ChunkInfo get_cur_chunk(Block *block) {
  for (int i = 0; i < chunk_idx; i++) {
    Block *cur_fencepost_start = chunk_arr[i].fencepost_start;
    Block *cur_fencepost_end = chunk_arr[i].fencepost_end;

    // Check if the block is within the allocated range of the current chunk
    if (block >= ADD_BYTES(cur_fencepost_start, kMetadataSize)  && block < cur_fencepost_end) {
      return chunk_arr[i];
    }
  }
  struct ChunkInfo invalid_chunk = {NULL, NULL, NULL};
  return invalid_chunk;
}

Block *find_free_block(size_t size) {
  Block *start = free_list_head->next;
  Block *best = NULL;
  size_t best_fit = __SIZE_MAX__;

  while (start != NULL && start != free_list_tail) {
    if (is_free(start) && block_size(start) >= size) {
      if (start->size < best_fit) {
        best_fit = block_size(start);
        best = start;
      }
    }
    start = start->next;
  }
  return best;
}


void insert_free_list(Block *block) {
  if (free_list_head->next == free_list_tail && free_list_tail->prev == free_list_head) {
    free_list_head->next = block;
    block->prev = free_list_head;
    block->next = free_list_tail;
    free_list_tail->prev = block;
    return;
  }

   // Traverse the free list to find the correct position
    Block *current = free_list_head->next;
    while (current != free_list_tail && current < block) {
        current = current->next;
    }

    // Insert the block before the current block
    block->next = current;
    block->prev = current->prev;

    // Adjust pointers of the neighboring blocks
    if (current->prev) {
        current->prev->next = block;
    }
    current->prev = block;
}

void remove_from_free_list(Block *block) {
  if (block != free_list_head && block != free_list_tail) {
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
  if (right != NULL) {
    right->size = size;
    right->allocated = 1;
    void *payload_ptr = ADD_BYTES(right, kMetadataSize);
    memset(payload_ptr, 0, size - kMetadataSize);
    return payload_ptr;
  }


}


void coalesce_adjacent_blocks(Block *free_block) {
    Block *cur = free_list_head->next;

    // Iterate through the free list
    while (cur != NULL && cur != free_list_tail) {
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
        if (cur != free_block && prev_block != free_list_head && is_free(prev_block) && next_block != free_list_tail && is_free(next_block)) {
            // Coalesce prev_block, free_block, and next_block
            prev_block->size += free_block->size + next_block->size;
            remove_from_free_list(free_block);  // Remove free_block
            remove_from_free_list(next_block);  // Remove next_block
            free_block = prev_block;  // Coalesced block remains at the left position (prev_block)
        }

        // Move to the next block
        cur = cur->next;
    }
}

int is_valid_block(Block *block) {
  for (int i = 0; i < chunk_idx; i++) {
    Block *cur_fencepost_start = chunk_arr[i].fencepost_start;
    Block *cur_fencepost_end = chunk_arr[i].fencepost_end;

    // Check if the block is within the allocated range of the current chunk
    if (block >= ADD_BYTES(cur_fencepost_start, kMetadataSize) && block < cur_fencepost_end) {
      return 1;  // Block is valid
    }
  }
  return 0;
}


void *my_malloc(size_t size) {
  if (size == 0 || size > kMaxAllocationSize) {
    return NULL;
  }
  
  size_t alloc_size = round_up(size + kMetadataSize, kAlignment);
  if (is_requested_memory == 0) {
    initialize();
    struct ChunkInfo chunk;
    int chunk_size = get_chunk_size(alloc_size);
    chunk = request_memory(chunk_size);
    chunk_arr[chunk_idx++] = chunk;
    insert_free_list(chunk.block_start);
  }

  Block *free_block = find_free_block(alloc_size);
  if (free_block == NULL) {
    // return NULL;
    // No suitable free block, request more memory from the kernel
    struct ChunkInfo new_chunk;
    int chunk_size = get_chunk_size(alloc_size);
    new_chunk = request_memory(chunk_size);
    chunk_arr[chunk_idx++] = new_chunk;
    insert_free_list(new_chunk.block_start);
    free_block = find_free_block(alloc_size);
  } 
  cur_free_block = free_block;
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

  if (!is_requested_memory) {
    free(ptr);
    return;
  }

  if (is_valid_block(block) == 0 && !is_free(block)) {
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
  struct ChunkInfo c;
  c = get_cur_chunk(cur_free_block);
  return c.block_start;
}

/* Returns the next block in memory */
Block *get_next_block(Block *block) {
  if (block == NULL) {
    return NULL;
  }

  if (!is_valid_block(block)) {
    return NULL;
  }

  struct ChunkInfo c = get_cur_chunk(block);
  
  Block* next_block = ADD_BYTES(block, block_size(block));
  if (next_block >= c.fencepost_end) {
    return NULL;
  }
  return next_block;
}

/* Given a ptr assumed to be returned from a previous call to `my_malloc`,
   return a pointer to the start of the metadata block. */
Block *ptr_to_block(void *ptr) {
  return ADD_BYTES(ptr, -((size_t) kMetadataSize));
}

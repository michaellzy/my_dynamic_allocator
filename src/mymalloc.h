#ifndef MYMALLOC_HEADER
#define MYMALLOC_HEADER

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#ifdef ENABLE_LOG
#define LOG(...) fprintf(stderr, "[malloc] " __VA_ARGS__);
#else
#define LOG(...)
#endif

#define N_LISTS 59

#define ADD_BYTES(ptr, n) ((void *) (((char *) (ptr)) + (n)))

#define ALLOCATED_MASK ((size_t)1)
#define SIZE_MASK (~ALLOCATED_MASK)

/** This is the Block struct, which contains all metadata needed for your 
 *  explicit free list. You are allowed to modify this struct (and will need to 
 *  for certain optimisations) as long as you don't move the definition from 
 *  this file. **/
typedef struct Block Block;
// typedef struct FreeBlock FreeBlock;
typedef struct Linker Linker;

// struct Block {
//   // Size of the block, including meta-data size.
//   size_t cur_size;
//   // Next and Prev blocks
//   Block *next;
//   Block *prev;
//   // Is the block allocated or not?
//   bool allocated;
// };

struct Block {
    size_t size;
    // bool allocated;
};

struct Linker {
    Linker *prev;
    Linker *next;
};

// struct FreeBlock {
//     size_t size;
//     bool allocated;
//     FreeBlock *next;
//     FreeBlock *prev;
// };

// struct ChunkInfo {
//   Block* fencepost_start;
//   Block* fencepost_end;
//   Block* block_start;
// };
struct ChunkInfo {
    Block* fencepost_start;
    Block* fencepost_end;
    Block* block_start;
};


// Word alignment
extern const size_t kAlignment;
// Minimum allocation size (1 word)
extern const size_t kMinAllocationSize;
// Size of meta-data per Block
extern const size_t kMetadataSize;
// Maximum allocation size (128 MB)
extern const size_t kMaxAllocationSize;
// Memory size that is mmapped (64 MB)
extern const size_t kMemorySize;

extern const size_t kAllocatedMetadataSize;
extern const size_t kFreeMetadataSize;

void initialize();
int get_chunk_size(size_t alloc_size);
struct ChunkInfo request_memory(int n);
struct ChunkInfo get_cur_chunk(Block *block);
// Block *find_free_block(size_t size);
Block *find_free_block(size_t size);
// void insert_free_list(Block *block);
void insert_free_list(Block *block);
// void remove_from_free_list(Block *block);
// void remove_from_free_list(Block *block);
// Block *split_block(Block *block, size_t size);
Block *split_block(Block *block, size_t size);
void splice_out_block(Block* block);
void coalesce_adjacent_blocks(Block *free_block);
int is_valid_block(Block *block);
void *my_malloc(size_t size);
void my_free(void *p);

/* Helper functions you are required to implement for internal testing. */
void set_allocated(Block* block, int allocated);
int is_free(Block *block);
void set_block_size(Block* block, size_t new_size);
size_t block_size(Block *block);

Block *get_start_block(void); 
Block *get_next_block(Block *block);
Block *get_prev_block(Block *block);
Block *ptr_to_block(void *ptr);
Linker *get_linker(Block* block);
Block *get_footer(void* ptr, size_t alloc_size);

#endif

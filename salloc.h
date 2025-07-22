#ifndef SALLOC_H
#define SALLOC_H

#include <stddef.h>

void * salloc(size_t block_size);
void * salloc_aligned(size_t block_size, size_t alignment);

void * srealloc(void * block, const size_t new_block_size);
void * srealloc_aligned(void * block, size_t new_block_size, size_t alignment);

void sfree(void * block);

size_t sblock_size(void * block);

void heap_dump();
void bitmap_dump();

#endif // SALLOC_H
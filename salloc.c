#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>

// -------
// DEFINES
// -------

#ifndef HEAP_SIZE
#define HEAP_SIZE 65536
#endif

#define DEFAULT_ALIGNMENT (_Alignof(max_align_t))

// ----------------
// GLOBAL VARIABLES
// ----------------

static uint8_t _heap[HEAP_SIZE];
static uint8_t _bitmap[HEAP_SIZE/CHAR_BIT];
static uint8_t _metadata_byte_size = 0;

// -------------
// HELPER MACROS
// -------------

#define INITIALIZED (_metadata_byte_size != 0)
#define _NTH_BYTE(nth_bit) (nth_bit / CHAR_BIT)
#define _BIT_IN_BYTE(nth_bit) (CHAR_BIT - 1 - (nth_bit % CHAR_BIT))

// ----------------
// HELPER FUNCTIONS
// ----------------

static inline void _set_bit(void *obj, const size_t nth_bit, const uint8_t value) {
    const uint8_t mask = 1U << _BIT_IN_BYTE(nth_bit);
    uint8_t * const byte_ptr = (uint8_t *)obj + _NTH_BYTE(nth_bit);

    *byte_ptr = (*byte_ptr & ~mask) | (value ? mask : 0);
}

static inline uint8_t _get_bit(void * obj, const size_t nth_bit) {
    return (*((uint8_t *)obj + _NTH_BYTE(nth_bit)) >> _BIT_IN_BYTE(nth_bit)) & 1U;
}

// X macros cuz why not
#define LIST_OF_BIT_SIZES \
    X(8) \
    X(16) \
    X(32) \
    X(64)

static inline void _init_metadata_byte_size() {
    if (_metadata_byte_size != 0) {return;}
    const uint16_t bits = ceil(log2(HEAP_SIZE+1));
    #define X(size) if (bits <= size) {_metadata_byte_size = size / CHAR_BIT; return;}
    LIST_OF_BIT_SIZES
    #undef X
}

static inline uint64_t _read_metadata(void * metablock) {
    switch (_metadata_byte_size * CHAR_BIT) {
        #define X(size) case size: return *(uint##size##_t *) metablock;
        LIST_OF_BIT_SIZES
        #undef X
        default: assert(0 && "Unreachable!");
    }
}

static inline void _write_metadata(void * metablock, uint64_t value) {
    switch (_metadata_byte_size * CHAR_BIT) {
        #define X(size) case size: *(uint##size##_t *) metablock = value; break;
        LIST_OF_BIT_SIZES
        #undef X
        default: assert(0 && "Unreachable!");
    }
}

static inline void * _metablock_ptr(void * block) {
    return (uint8_t *) block - _metadata_byte_size;
}

static inline size_t _heap_idx_from_ptr(void * block) {
    return (uint8_t *) block - _heap - _metadata_byte_size;
}

static inline size_t _metablock_size(void * block) {
    return _read_metadata(_metablock_ptr(block)) + _metadata_byte_size;
}

// --------------
// IMPLEMENTATION
// --------------

void * salloc_aligned(const size_t block_size, const size_t alignment) {
    if (block_size == 0) {return NULL;}
    if (block_size > HEAP_SIZE) {return NULL;}
    if (alignment == 0) {return NULL;}

    if (_metadata_byte_size == 0) {_init_metadata_byte_size();}
    const size_t metablock_size = block_size + _metadata_byte_size;

    uint8_t * metablock_ptr = NULL;
    size_t metablock_idx = 0;
    size_t free_bytes_counter = 0;

    for (size_t i = 0; i < HEAP_SIZE; i++) {
        // skip already issued bytes
        if (_get_bit(_bitmap, i) == 1) {
            free_bytes_counter = 0;
            metablock_idx = 0;
            metablock_ptr = NULL;
            continue;
        }

        // set pointer to the metablock to be issued
        if (metablock_ptr == NULL) {
            metablock_ptr = _heap + i;
            // reset metablock pointer and continue searching if the block address is not aligned
            if (((uintptr_t)metablock_ptr+_metadata_byte_size) % alignment != 0) {
                metablock_ptr = NULL;
                continue;
            }
            metablock_idx = i;
        }

        free_bytes_counter++;
        if (free_bytes_counter == metablock_size) {
            // mark bytes as issued in bitmap
            for (size_t j = 0; j < metablock_size; j++) {
                _set_bit(_bitmap, metablock_idx + j, 1);
            }
            _write_metadata(metablock_ptr, block_size);
            // return pointer to the block (data), not metadata
            return metablock_ptr + _metadata_byte_size;
        }
    }
    return NULL;
}

void * salloc(const size_t size) {
    return salloc_aligned(size, DEFAULT_ALIGNMENT);
}

void sfree(void * block) {
    if (!INITIALIZED) {return;}
    const size_t block_size = _metablock_size(block);
    const size_t block_idx = _heap_idx_from_ptr(block);

    for (size_t i = 0; i < block_size; i++) {
        _set_bit(_bitmap, block_idx + i, 0);
    }
}

void * srealloc_aligned(void * block, const size_t new_size, const size_t alignment) {
    if (!INITIALIZED) {return NULL;}
    void * new_block = salloc_aligned(new_size, alignment);
    if (new_block == NULL) {return NULL;}
    memcpy(new_block, block, _read_metadata(_metablock_ptr(block)));
    sfree(block);
    return new_block;
}

void * srealloc(void * block, const size_t new_size) {
    return srealloc_aligned(block, new_size, DEFAULT_ALIGNMENT);
}

size_t sblock_size(void * block) {
    if (!INITIALIZED) {return 0;}
    return _read_metadata(_metablock_ptr(block));
}

void heap_dump() {
    printf("Heap size: %zu\n", (size_t)HEAP_SIZE);
    for (size_t i = 0; i < HEAP_SIZE; i++) {
        printf("%03u ", _heap[i]);
    }
    putchar('\n');
}

void bitmap_dump() {
    printf("Bitmap size (bytes): %zu\n", (size_t)HEAP_SIZE / CHAR_BIT);
    for (size_t i = 0; i < HEAP_SIZE; i++) {
        printf("%u ", _get_bit(_bitmap, i));
    }
    putchar('\n');
}

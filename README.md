# SALLOC (Stack Allocator)
By Miroslav Kramár, with love

> [!WARNING]
> This is an experiment. I discourage anyone from using this library seriously. The allocation logic is probably too complicated and slow anyways.

Malloc/free-like API to perform dynamic allocations on the stack.

This implementation assumes C11 standard. However, with slight tweaks, C99 is possible.

## Example usage

> **NOTE**: Every function performing allocation will return `NULL` when it fails to find a large enough block. It allso returns `NULL` when provided with invalid parameters, such as requesting a block of size `0` or block with `0` alignment.

### Basic example

```c
#include <stdio.h>
#include "salloc.h"

int main() {
    // allocation
    const size_t SIZE = 10;
    int * a = salloc(SIZE * sizeof(int));

    // write
    for (int i = 0; i < SIZE; i++) {
        a[i] = i;
    }

    // read
    for (int i = 0; i < SIZE; i++) {
        printf("%d\n", a[i]);
    }

    // deallocation
    sfree(a);
    return 0;
}
```

### Reallocation
```c
#include <stdio.h>
#include <string.h>

#include "salloc.h"

int main() {
    const size_t SIZE = 10;
    int * a = salloc(SIZE * sizeof(int));
    if (a == NULL) return 1;

    memset(a, 11, sblock_size(a));

    int * tmp = srealloc(a, SIZE * 2 * sizeof(int));
    if (tmp == NULL) return 1;

    memset(a, 22, sblock_size(a));

    heap_dump();
    bitmap_dump();
    sfree(a);
    return 0;
}
```

### Alignment
```c
#include <stdio.h>
#include <stdalign.h>
#include <string.h>

#include "salloc.h"

int main() {
    // allocate a block of memory aligned to 16 bytes
    int * a = salloc(sizeof(*a) * 10);
    // allocate a block of memory with specific alignment
    int * b = salloc_aligned(sizeof(*a) * 10, alignof(*a));

    // write to memory
    memset(a, 11, sblock_size(a));
    memset(b, 22, sblock_size(b));

    // heap and bitmap raw value dump
    heap_dump();
    bitmap_dump();
    putchar('\n');

    // freeing the allocated memory
    sfree(a);
    sfree(b);

    // view heap and bitmap after deallocation
    heap_dump();
    bitmap_dump();
    putchar('\n');

    return 0;
}
```

## Building

The library implementation uses only standard C. It can be compiled as:

```bash
gcc main.c salloc.c
```

Default heap size is 8192. If you want custom heap size, then compile with

```bash
gcc main.c salloc.c -DHEAP_SIZE=65536
```

Or, of course, tweak the source code.

## A little story on how it was made

### Global state
The whole library is built around 3 global variables, `heap`, `bitmap` and `metadata_byte_size`(more on that later).

Heap is a statically allocated array of `uint8_t`. It contains all the issued blocks of memory by the stack allocator.

Bitmap has at least the same ammount of bits as there are bytes in the heap. It tracks all the issued bytes in the heap by storing their state in bits on the corresponding indices.

### Allocation
When a block of certain size is requested, the allocator goes through the bitmap until it finds a not-yet-issued memory. It first counts whether or not there are enough free bytes to match the requested number of bytes + metadata. If so, it then marks all the corresponding bits in the bitmap as issued and returns a pointer to the block.

### Metadata?

As it might have already occurred to you, the bitmap is used for allocation by tracking which bytes are already issued and which are not.

When we want to free the issued memory, we can simply compute the starting index of this memory by subtracting the heap pointer from the issued block pointer. But to be able to free the memory, we must also know the size of the issued block. And the information about size is stored as metadata right before the block itself.

When we allocate the block, we actually allocate a bit more, exactly `metadata_byte_size` more, creating a "metablock". Of course, we do not want to return a pointer to the whole "metablock", for the sake of ergonomics. Instead, we offset the pointer by `metadata_byte_size`, so it points to the block (data) part. Finally, we can return the pointer.

### metadata_byte_size?

When designing the library, I thought about how many bytes should the metadata occupy. `sizeof(size_t)` was the first candidate, and a good candidate. But not good enough. Since we are allocating on the stack, which is significantly smaller than the heap, we should optimize for memory usage.

If the user, for example, decides to define the heap as 4096 bytes large, then the maximum sized block of memory we can possibly issue is exactly that, minus metadata. Full `size_t` worth of memory (8 bytes on a modern 64bit system) is arguably overkill in this case. 4096 fits well into just 2 bytes (`uint16_t`) of memory. So I decided to compute the number of bytes the metadata of each block should occupy based on the heap size.

The formula for computing the minimum number of bytes to store the block size is:

```
bytes = ceil(log2(HEAP_SIZE+1)) / CHAR_BIT
```

Ceil? And base 2 logarithm?! Yikes! No way we can compute this at compile time. At least not in C. But no way I am switching languages just because of this minor inconvenience! So runtime it is.

At compile time, `metadata_byte_size` is initialized with `0`. Every call to `salloc` or `salloc_aligned` then checks whether or not it was set to a nonzero value (like, actually initialized). If not, it then performs the necessary computation mentioned above (plus some adjustments). Now, the metadata byte size is known. Hurray!

Of course, one could ask what happens, when the user sets the heap size to something ridiculously small, or zero. And my answer is: I don't know! Why would you do that?! Look, I know making stuff bulletproof is exciting. But sometimes breaking it is at least as, if not more exciting. And I've already warned you to not use this library for anything even remotely serious. So if you want to break it, go on. I am not stopping you. Although I could add some static asserts to assure some minimal heap size. Someday. Maybe.

## Alignment

I've learned to compile my C programs with `-fsanitize=address,undefined`, ASan and UBSan. They both proved to be a very useful tools, at least easing the pain of searching for bugs in C programs. You can read more about it [here](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html).

But why am I talking about this? Well, when I first tested the library, UBSan reported unaligned memory, which is actually an example of undefined behavior. Since I compiled the program without optimizations, fortunately, no [nasal demons](https://groups.google.com/g/comp.std.c/c/ycpVKxTZkgw/m/S2hHdTbv4d8J) seemed to be involved. But no runtime warning looks good, eh?

Since I was oblivious to the fact that unaligned read and write are actually a huge problem, I happily issued blocks with starting addres at... wherever.

But issuing aligned memory, e.g. at address divisible by the alignment of the data type we want to read/write seems to be actually very important for the CPU accessing the data on such address. From what I've been told, some processors can handle unaligned memory, like my 10th gen Intel Core i5, but for a performance cost. And some processors refuse to bother and just straight up terminate the program.

So I just introduced `..._aligned` versions of `salloc` and `srealloc` and performed some additional computation to actually issue an aligned block. And also made the `salloc` and `srealloc` both use the max alignment, which is a very sad `16` bytes, but hey, it works!
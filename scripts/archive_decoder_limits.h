#pragma once
/* Build-only libarchive hardening. Included immediately after archive_platform.h
 * so upstream feature/configuration macros precede standard system headers.
 * Keep free() unchanged: allocations remain ordinary C runtime allocations.
 * Decoder dictionaries have additional liblzma/zstd native limits.
 */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#define DRIFTER_ARCHIVE_ALLOCATION_LIMIT ((size_t)128 * 1024 * 1024)
static inline void* drifter_archive_bounded_malloc(size_t size) {
    if (size > DRIFTER_ARCHIVE_ALLOCATION_LIMIT) { errno = ENOMEM; return NULL; }
    return malloc(size);
}
static inline void* drifter_archive_bounded_calloc(size_t count, size_t size) {
    if (size && count > DRIFTER_ARCHIVE_ALLOCATION_LIMIT / size) { errno = ENOMEM; return NULL; }
    return calloc(count, size);
}
static inline void* drifter_archive_bounded_realloc(void* memory, size_t size) {
    if (size > DRIFTER_ARCHIVE_ALLOCATION_LIMIT) { errno = ENOMEM; return NULL; }
    return realloc(memory, size);
}
#define malloc drifter_archive_bounded_malloc
#define calloc drifter_archive_bounded_calloc
#define realloc drifter_archive_bounded_realloc

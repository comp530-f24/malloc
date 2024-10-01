/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4;
 * indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

// PID: 9DigitPidNoSpacesOrDashes
// I pledge the COMP530 honor code.

/* Tar Heels Allocator
 *
 * Simple Hoard-style malloc/free implementation.
 * Not suitable for use for large allocatoins, or
 * in multi-threaded programs.
 *
 * to use:
 * $ export LD_PRELOAD=/path/to/th_alloc.so <your command>
 */

/* Hard-code some system parameters */

#define SUPER_BLOCK_SIZE 4096
#define SUPER_BLOCK_MASK (~(SUPER_BLOCK_SIZE - 1))
#define MIN_ALLOC 32 /* Smallest real allocation.  Round smaller mallocs up */
#define MAX_ALLOC                                                              \
  2048 /* Fail if anything bigger is attempted.                                \
        * Challenge: handle big allocations */
#define RESERVE_SUPERBLOCK_THRESHOLD 2

#define FREE_POISON 0xab
#define ALLOC_POISON 0xcd

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <valgrind/memcheck.h>

void *mmap_wrap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset);
int munmap_wrap(void *addr, size_t length);

#define assert(cond)                                                           \
  if (!(cond))                                                                 \
  __builtin_trap()

/* Object: One return from malloc/input to free. */
struct __attribute__((packed)) object {
  union {
    struct object *next; // For free list (when not in use)
    char *raw;           // Actual data
  };
};

/* Super block bookeeping; one per superblock.  "steal" the first
 * object to store this structure
 */
struct __attribute__((packed)) superblock_bookkeeping {
  struct superblock_bookkeeping *next; // next super block
  struct object *free_list;
  // Free count in this superblock
  uint8_t free_count; // Max objects per superblock is 128-1, so a byte is
                      // sufficient
  uint8_t level;
};

/* Superblock: a chunk of contiguous virtual memory.
 * Subdivide into allocations of same power-of-two size. */
struct __attribute__((packed)) superblock {
  struct superblock_bookkeeping bkeep;
  void *raw; // Actual data here
};

/* The structure for one pool of superblocks.
 * One of these per power-of-two */
struct superblock_pool {
  struct superblock_bookkeeping *next;
  uint64_t free_objects; // Total number of free objects across all superblocks
  uint64_t whole_superblocks; // Superblocks with all entries free
};

// 10^5 -- 10^11 == 7 levels
#define LEVELS 7
static struct superblock_pool levels[LEVELS] = {
    {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
    {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}};

/* Data structure to track large objects (bigger than MAX_ALLOC.
 * We will just do a simple linked-list by default.
 *
 * Extra credit to do something more substantial (or before seeing this code).
 */
struct big_object {
  struct big_object *next;
  void *addr;
  size_t size;
};

// List of big objects
static struct big_object *big_object_list = NULL;

/* Convert the size to the correct power of two.
 * Recall that the 0th entry in levels is really 2^5,
 * the second level represents 2^6, etc.
 *
 * Return the index to the appropriate level (0..6), or
 * -1 if the size is too large.
 */

int size2level(size_t size) {
  /* Your code here. */
  // Temporarily suppress the compiler warning that size is unused
  // You should remove the following line
  (void)(size);
  return 0;
}

static void enable_bookkeeping() {
  for (int i = 0; i < LEVELS; i++) {
    struct superblock_bookkeeping *sb = levels[i].next;
    while (sb) {
      VALGRIND_MAKE_MEM_DEFINED(sb, sizeof(struct superblock_bookkeeping));
      sb = sb->next;
    }
  }
}

static void disable_bookkeeping() {
  for (int i = 0; i < LEVELS; i++) {
    struct superblock_bookkeeping *sb = levels[i].next;
    struct superblock_bookkeeping *tmp;
    while (sb) {
      tmp = sb->next;
      VALGRIND_MAKE_MEM_NOACCESS(sb, sizeof(struct superblock_bookkeeping));
      sb = tmp;
    }
  }
}

/* This function allocates and initializes a new superblock.
 *
 * Note that a superblock in this lab is only one 4KiB page, not
 * 8 KiB, as in the hoard paper.
 *
 * This design sacrifices the first entry in every superblock
 * to store a superblock_bookkeeping structure.  Yes,
 * it is a bit wasteful, but let's keep the exercise simple.
 *
 * power: the power of two to store in this superblock.  Note that
 *        this is offset by 5; so a power zero means 2^5.
 *
 * Return value: a struct superblock_bookkeeping, which is
 * embedded at the start of the superblock.  Or NULL on failure.
 */
static inline struct superblock_bookkeeping *alloc_super(int power) {

  void *page;
  struct superblock *sb;
  int free_objects = 0, bytes_per_object = 0;
  char *cursor;
  // Your code here
  // Allocate a page of anonymous memory
  // WARNING: DO NOT use brk---use mmap, lest you face untold suffering

  sb = (struct superblock *)page;
  // Put this one the list.
  sb->bkeep.next = levels[power].next;
  levels[power].next = &sb->bkeep;
  levels[power].whole_superblocks++;
  sb->bkeep.level = power;
  sb->bkeep.free_list = NULL;

  // Your code here: Calculate and fill the number of free objects in this
  // superblock
  //  Be sure to add this many objects to levels[power]->free_objects, reserving
  //  the first one for the bookkeeping.
  // Be sure to set free_objects and bytes_per_object to non-zero values.

  // The following loop populates the free list with some atrocious
  // pointer math.  You should not need to change this, provided that you
  // correctly calculate free_objects.

  cursor = (char *)sb;
  // skip the first object
  for (cursor += bytes_per_object; free_objects--; cursor += bytes_per_object) {
    // Place the object on the free list
    struct object *tmp = (struct object *)cursor;
    tmp->next = sb->bkeep.free_list;
    sb->bkeep.free_list = tmp;
  }

  VALGRIND_MAKE_MEM_NOACCESS(page + sizeof(struct superblock_bookkeeping),
                             SUPER_BLOCK_SIZE -
                                 sizeof(struct superblock_bookkeeping));
  return &sb->bkeep;
}

void *malloc(size_t size) {
  enable_bookkeeping();
  struct superblock_pool *pool;
  struct superblock_bookkeeping *bkeep;
  void *rv = NULL;
  int power = size2level(size);

  // Handle bigger allocations with mmap, and a simple list
  if (size > MAX_ALLOC) {
    // Why, yes we can do a recursive malloc.  But carefully...
    struct big_object *biggun = malloc(sizeof(struct big_object));

    // Malloc disabled bookkeeping memory. We do not need to re-enable
    // bookkeeping for the following.
    biggun->next = big_object_list;
    biggun->addr = mmap_wrap(NULL, size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(biggun->addr);
    biggun->size = size;
    big_object_list = biggun;
    return biggun->addr;
  }

  // Delete the following two lines
  errno = -ENOMEM;
  return rv;

  pool = &levels[power];

  if (!pool->free_objects) {
    bkeep = alloc_super(power);
  } else {
    bkeep = pool->next;
  }

  for (; bkeep != NULL; bkeep = bkeep->next) {
    if (bkeep->free_count) {
      // Mark the corresponding memory as accessible in Valgrind.
      VALGRIND_MAKE_MEM_DEFINED(bkeep->free_list, (1 << (power + 5)));
      struct object *cursor = bkeep->free_list;
      /* Remove an object from the free list. */
      // Your code here
      //
      // NB: If you take the first object out of a whole
      //     superblock, decrement levels[power]->whole_superblocks
      // Temporarily suppress the compiler warning that cursor is unused
      // You should remove the following line
      (void)(cursor);
      break;
    }
  }

  // assert that rv doesn't end up being NULL at this point
  assert(rv != NULL);

  /* Exercise 3: Poison a newly allocated object to detect init errors.
   * Hint: use ALLOC_POISON
   */

  disable_bookkeeping();
  return rv;
}

static inline struct superblock_bookkeeping *obj2bkeep(void *ptr) {
  uint64_t addr = (uint64_t)ptr;
  addr &= SUPER_BLOCK_MASK;
  return (struct superblock_bookkeeping *)addr;
}

void free(void *ptr) {

  // Just ignore free of a null ptr
  if (ptr == NULL)
    return;

  enable_bookkeeping();
  struct superblock_bookkeeping *bkeep = obj2bkeep(ptr);
  int power = bkeep->level;

  // We need to check for free of any large objects first.
  {
    struct big_object *tmp, *last;
    for (tmp = big_object_list, last = NULL; tmp; last = tmp, tmp = tmp->next) {
      if (tmp->addr == ptr) {
        // We found it
        // Unmap the object
        munmap_wrap(tmp->addr, tmp->size);
        // Fix up the list
        if (!last) {
          big_object_list = tmp->next;
        } else {
          last->next = tmp->next;
        }
        // Free the node
        free(tmp);

        // Do not have to disable bookkeeping here: already and must have been
        // disabled in free().
        return;
      }
    }
  }

  // Your code here.
  //   Be sure to put this back on the free list, and update the
  //   free count.  If you add the final object back to a superblock,
  //   making all objects free, increment whole_superblocks.

  /* Exercise 3: Poison a newly freed object to detect use-after-free errors.
   * Hint: use FREE_POISON.
   */

  // Mark the object just freed to be inaccessible by valgrind.
  VALGRIND_MAKE_MEM_NOACCESS(bkeep->free_list, (1 << (power + 5)));

  while (levels[power].whole_superblocks > RESERVE_SUPERBLOCK_THRESHOLD) {
    // Exercise 4: Your code here
    // Remove a whole superblock from the level
    // Return that superblock to the OS, using mmunmap
    break; // hack to keep this loop from hanging; remove in ex 4
  }
  disable_bookkeeping();
}

void *mmap_wrap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset) {
  void *ret = mmap(addr, length, prot, flags, fd, offset);
  VALGRIND_MALLOCLIKE_BLOCK(ret, length, 0, 0);
  return ret;
}

int munmap_wrap(void *addr, size_t length) {
  int ret = munmap(addr, length);
  VALGRIND_FREELIKE_BLOCK(addr, 0);
  return ret;
}

// Do NOT touch this - this will catch any attempt to load this into a
// multi-threaded app
int pthread_create(void __attribute__((unused)) * x, ...) { exit(-ENOSYS); }

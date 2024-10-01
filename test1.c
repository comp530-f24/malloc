/* Malloc unit test
 *
 * Don Porter - COMP 530 - UNC Chapel Hill
 *
 * This utility allocates and frees memory in different patterns, keeping
 * track of the requested sizes, overwriting with junk, and looking for errors
 * or inconsistencies.
 */

#include <assert.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <valgrind/memcheck.h>

#define FREE_POISON 0xab
#define ALLOC_POISON 0xcd

#define LEVELS 7

static int leaked;
static int dubious;
static int reachable;
static int suppressed;

// We keep track of allocated memory in a structure on the data segment,
// so we don't need to malloc our bookeeping too
struct thingy {
  char *memory;
  size_t size;
  char pattern;
  bool valid;
};

#define MAX_THINGIES 256
static struct thingy thingies[MAX_THINGIES];

// reference to size2level implementation
int (*size2level)(size_t);

/* Check that all thingies have a correct value */
static bool values_ok(void) {
  int i, j;
  for (i = 0; i < MAX_THINGIES; i++) {
    if (thingies[i].valid) {
      for (j = 0; j < thingies[i].size; j++) {
        if (thingies[i].memory[j] != thingies[i].pattern)
          return false;
      }
    }
  }
  return true;
}

/* Allocate a thingy */
static void gimme(size_t size, bool check_poison) {
  void *x = malloc(size);
  int i = 0;
  bool found = false;

  if (check_poison) {
    for (i = 0; i < size; i++) {
      char *y = (char *)x;
      assert(y[i] == (char)ALLOC_POISON);
    }
  }

  for (i = 0; i < MAX_THINGIES; i++) {
    if (!thingies[i].valid) {
      if (!found) {
        found = true;
        // Add an entry to track this allocation
        thingies[i].memory = x;
        thingies[i].size = size;
        thingies[i].pattern = (char)i;
        // Fill this element with junk
        memset(x, (char)i, size);
        thingies[i].valid = true;
      }
    } else {
      // Assert that we are not allocating twice
      assert(thingies[i].memory != x);
      // Check that the expected pattern is preserved
      int j;
      for (j = 0; j < thingies[i].size; j++) {
        assert(thingies[i].memory[j] == thingies[i].pattern);
      }
    }
  }
  // Should always find a slot
  assert(found);
}

/* Free a thingy, and clear bookkeeping */
static void heego(void *x) {
  int i = 0;
  bool found = false;
  for (i = 0; i < MAX_THINGIES; i++) {

    if (thingies[i].valid) {
      // Check that the expected pattern is preserved
      int j;
      for (j = 0; j < thingies[i].size; j++) {
        assert(thingies[i].memory[j] == thingies[i].pattern);
      }

      if (thingies[i].memory == x) {
        assert(!found);
        found = true;
        thingies[i].valid = false;
        free(x);
      }
    }
  }

  // Should always find the thing we free
  assert(found);
}

// Test 1: I'll take 32 of everything!
static void test1(bool test_poison) {
  int i, j;
  for (i = 0; i < 32; i++) {
    for (j = 0; j < 7; j++) {
      gimme(1 << (j + 5), test_poison);
    }
  }
  assert(values_ok());
}

// Test 2: freed memory gets recycled
static void test2(bool test_poison) {
  int i, j;
  for (i = 0; i < 32; i++) {
    for (j = 0; j < 7; j++) {
      gimme(1 << (j + 5), test_poison);
    }
  }
  assert(values_ok());
  // Free everything
  for (i = 0; i < 224; i++) {
    assert(thingies[i].valid);
    heego(thingies[i].memory);
  }
}

// Test 1c: Allocate, free every other object, re-allocate - look for
// corruptions
static void test3(bool test_poison) {
  int i, j;
  for (i = 0; i < 32; i++) {
    for (j = 0; j < 7; j++) {
      gimme(1 << (j + 5), test_poison);
    }
  }
  assert(values_ok());
  // Release every other object
  for (i = 0; i < 224; i++) {
    assert(thingies[i].valid);
    if (i % 2) {
      heego(thingies[i].memory);
      // If test_poison and object is smaller than 2048, see if it is poisoned
      // correctly. We should be able to do this safely, since one object will
      // hold the superblock
      if (test_poison && thingies[i].size < 2048) {
        // Skip first 8 bytes, for next pointer
        for (j = 8; j < thingies[i].size; j++) {
          char *y = (char *)thingies[i].memory;
          assert(y[j] == (char)FREE_POISON);
        }
      }
    }
  }
  assert(values_ok());

  // Reallocate every other object
  for (i = 0; i < 224; i++) {
    if (i % 2) {
      assert(!thingies[i].valid);
      gimme(i, test_poison);
    }
  }

  assert(values_ok());

  // Free everything
  for (i = 0; i < 224; i++) {
    assert(thingies[i].valid);
    heego(thingies[i].memory);
  }
}

// Test 1e: Check that freeing enough superblocks
//    actually releases one to the OS
//    Register a signal handler, and catch/handle the fault
static bool testing_free = false;

void handle_sigsegv(int sig) {
  if (testing_free) {
    printf("Test %d completed ok\n\n", 7);
    exit(0);
  } else {
    printf("Got an unexpected signal.  Uh oh\n");
    exit(-1);
  }
}

static void test4(void) {
  int i;
  for (i = 0; i < 3; i++) {
    gimme(2048, true);
  }
  // Register the signal handler
  __sighandler_t rv = signal(SIGSEGV, handle_sigsegv);
  assert(rv != SIG_ERR);
  // Free the three objects
  for (i = 0; i < 3; i++) {
    heego(thingies[i].memory);
  }
  testing_free = true;
  // Try touching the three objects
  for (i = 0; i < 3; i++) {
    char *y = (char *)thingies[i].memory;
    y[8] = '\0';
  }
  // Should not get here
  assert(0);
}

// Self test for starter code
static void test5(void) {
  // Allocate a few big thingies, and some small ones
  void *x = malloc(8192);
  void *z = malloc(32);
  void *y = malloc(4096);
  // Free it
  free(x);
  free(z);
  free(y);
}

// test for freeing NULL a bunch of times
static void test6(void) {
  for (int i = 0; i < 10; i++)
    free(NULL);
}

// test for freeing in random order
static void test7(bool test_poison) {
  void *blocks[10];
  int N = 10;
  for (int i = 0; i < N; i++) {
    gimme(32, test_poison);
  }
  for (int i = 0; i < N; i++) {
    blocks[i] = thingies[i].memory;
  }
  srand(time(NULL)); // Seed random number generator
  for (int i = 0; i < N;) {
    int idx = rand() % N;
    if (blocks[idx] != NULL) {
      heego(blocks[idx]);
      blocks[idx] = NULL; // Ensure we don't try to free this block again
      i++;
    }
  }
}

// test size2level helper method
static void test8() {
  size_t inputs[6] = {0, 32, 33, 64, 65, 1999};
  int expected[6] = {0, 0, 1, 1, 2, 6};

  for (int i = 0; i < sizeof(inputs) / sizeof(size_t); i++) {
    assert((*size2level)(inputs[i]) == expected[i]);
  }
}

int main(int argc, char **argv) {
  int i = 0;

  if (argc < 2) {
    printf("Must give at least 1 argument to select test.\n");
    return -1;
  }

  // Initialize the array to invalid
  for (i = 0; i < MAX_THINGIES; i++) {
    thingies[i].valid = false;
  }

  int test = atoi(argv[1]);

  switch (test) {
  case 1:
    test1(false);
    // When student run this locally, do not execute the if statement
    // because this leads to an error in valgrind output, which might cause the student to panic.
    // In autograder, this if statement will be executed.
    if(RUNNING_ON_VALGRIND && !isatty(0)) {
      VALGRIND_DO_LEAK_CHECK;
      VALGRIND_COUNT_LEAK_BLOCKS(leaked, dubious, reachable, suppressed);
      assert(leaked == 0);
      // TA solution exits with 55 superblocks still allocated.
      assert((dubious + reachable) < 100);
    }
    break;
  case 2:
    test2(false);
    // When student run this locally, do not execute the if statement
    // because this leads to an error in valgrind output, which might cause the student to panic.
    // In autograder, this if statement will be executed.
    if(RUNNING_ON_VALGRIND && !isatty(0)) {
      VALGRIND_DO_LEAK_CHECK;
      VALGRIND_COUNT_LEAK_BLOCKS(leaked, dubious, reachable, suppressed);
      assert(leaked == 0);
      // TA solution exits with 12 superblocks still allocated.
      assert((dubious + reachable) < 25);
    }
    break;
  case 3:
    test3(false);
    // When student run this locally, do not execute the if statement
    // because this leads to an error in valgrind output, which might cause the student to panic.
    // In autograder, this if statement will be executed.
    if(RUNNING_ON_VALGRIND && !isatty(0)) {
      VALGRIND_DO_LEAK_CHECK;
      VALGRIND_COUNT_LEAK_BLOCKS(leaked, dubious, reachable, suppressed);
      assert(leaked == 0);
      // TA solution exits with 12 superblocks still allocated.
      assert((dubious + reachable) < 25);
    }
    break;
  case 4:
    test1(true);
    break;
  case 5:
    test2(true);
    break;
  case 6:
    test3(true);
    break;
  case 7:
    test4();
    break;
  case 8:
    test5();
    break;
  case 9:
    test6();
    break;
  case 10:
    test7(false);
    break;
  case 11:
    test7(true);
    break;
  case 12:
    void *lib = dlopen("th_alloc.so", RTLD_NOW);
    if (!lib) {
      printf(
          "Unable to load shared library. Did student code compile correctly?");
      assert(false);
    }
    size2level = dlsym(lib, "size2level");
    if (!size2level) {
      printf("Not able to find size2level in student .so\nMake sure size2level "
             "is implemented, non-static, and not inlined\n");
      assert(false);
    }
    test8();
    size2level = NULL;
    dlclose(lib);
    break;
  default:
    printf("Unknown test\n");
    return -1;
  }

  printf("Test %d completed ok\n\n", test);

  return 0;
}

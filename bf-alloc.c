// ==============================================================================
/**
 * bf-alloc.c
 *
 * A _best-fit_ heap allocator.  This allocator uses a _doubly-linked free list_
 * from which to allocate the best fitting free block.  If the list does not
 * contain any blocks of sufficient size, it uses _pointer bumping_ to expand
 * the heap.
 **/
// ==============================================================================



// ==============================================================================
// INCLUDES

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "safeio.h"
// ==============================================================================



// ==============================================================================
// TYPES AND STRUCTURES

/** The header for each allocated object. */
typedef struct header {

  /** Pointer to the next header in the list. */
  struct header* next;

  /** Pointer to the previous header in the list. */
  struct header* prev;

  /** The usable size of the block (exclusive of the header itself). */
  size_t         size;

  /** Is the block allocated or free? */
  bool           allocated;

} header_s;
// ==============================================================================



// ==============================================================================
// MACRO CONSTANTS AND FUNCTIONS

/** The system's page size. */
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

/**
 * Macros to easily calculate the number of bytes for larger scales (e.g., kilo,
 * mega, gigabytes).
 */
#define KB(size)  ((size_t)size * 1024)
#define MB(size)  (KB(size) * 1024)
#define GB(size)  (MB(size) * 1024)

/** The virtual address space reserved for the heap. */
#define HEAP_SIZE GB(2)

/** Given a pointer to a header, obtain a `void*` pointer to the block itself. */
#define HEADER_TO_BLOCK(hp) ((void*)((intptr_t)hp + sizeof(header_s)))

/** Given a pointer to a block, obtain a `header_s*` pointer to its header. */
#define BLOCK_TO_HEADER(bp) ((header_s*)((intptr_t)bp - sizeof(header_s)))
// ==============================================================================


// ==============================================================================
// GLOBALS

/** The address of the next available byte in the heap region. */
static intptr_t free_addr  = 0;

/** The beginning of the heap. */
static intptr_t start_addr = 0;

/** The end of the heap. */
static intptr_t end_addr   = 0;

/** The head of the free list. */
static header_s* free_list_head = NULL;

/** The head of the allocated list. */
static header_s* alloc_list_head = NULL;
// ==============================================================================



// ==============================================================================
/**
 * The initialization method.  If this is the first use of the heap, initialize it.
 */

void init () {

  // Only do anything if there is no heap region (i.e., first time called).
  if (start_addr == 0) {

    DEBUG("Trying to initialize");
    
    // Allocate virtual address space in which the heap will reside. Make it
    // un-shared and not backed by any file (_anonymous_ space).  A failure to
    // map this space is fatal.
    void* heap = mmap(NULL,
		      HEAP_SIZE,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS,
		      -1,
		      0);
    if (heap == MAP_FAILED) {
      ERROR("Could not mmap() heap region");
    }

    // Hold onto the boundaries of the heap as a whole.
    start_addr = (intptr_t)heap;
    end_addr   = start_addr + HEAP_SIZE;
    free_addr  = start_addr;

    // DEBUG: Emit a message to indicate that this allocator is being called.
    DEBUG("bf-alloc initialized");

  }

} // init ()
// ==============================================================================


// ==============================================================================
/**
 * Allocate and return `size` bytes of heap space.  Specifically, search the
 * free list, choosing the _best fit_.  If no such block is available, expand
 * into the heap region via _pointer bumping_.
 *
 * \param size The number of bytes to allocate.
 * \return A pointer to the allocated block, if successful; `NULL` if unsuccessful.
 */
void* malloc (size_t size) {

  init();

  // return NULL if size requested is 0
  if (size == 0) {
    return NULL;
  }

  header_s* current = free_list_head;  // pointer to the free block list
  header_s* best    = NULL;  // pointer to our best-fit block

  /****************************************
   * A loop to find a best-fit memory block
   ***************************************/
  
  while (current != NULL) {

    // if the current block is marked as allocated, there is an error
    if (current->allocated) {
      ERROR("Allocated block on free list", (intptr_t)current);
    }

    // if we found our first block that is bigger than the requested size, or
    //   if our current block is bigger than the requested size and smaller than our previous best-fit block
    if ( (best == NULL && size <= current->size) ||
	 (best != NULL && size <= current->size && current->size < best->size) ) {
      best = current;  // ... then set the current block as the best-fit block
    }

    // if our best-fit block is exactly the size we need, exit from the loop
    if (best != NULL && best->size == size) {
      break;
    }

    // proceed to next free block in the list
    current = current->next;
    
  }

  void* new_block_ptr = NULL; // pointer that will point to our newly allocated block

  /****************************************
   * If we have found a best-fit block
   ***************************************/  
  if (best != NULL) {

    /****************************************
     * Remove our best-fit block from the 
     * free block list
     ***************************************/
    
    // if our best-fit block was the first block in our free block list
    if (best->prev == NULL) {
      free_list_head   = best->next;  // ... then make the next free block the new first element in our free block list
    } else {
      best->prev->next = best->next; // ...otherwise, the previous free block's 'next' pointer will point to our best-fit block's 'next' free block address
    }

    // if our best-fit block was not the last block in the free block list
    if (best->next != NULL) {
      best->next->prev = best->prev; // ...then the next free block's 'prev' pointer will point to our best-fit block's 'prev' address
    }
    
    /****************************************
     * Add our best-fit block to the 
     * allocated block list
     ***************************************/

    best->next = alloc_list_head; // set our best-fit block's 'next' pointer to point to first element in our allocated block list
    alloc_list_head  = best;  //  our allocated block list pointer will now point to our best-fit block's header
    best->prev = NULL; // set our best-fit block's 'prev' point to null, as it will be the first item in the allocated block list

    // if there was already a block in the allocated block list
    if (best->next != NULL) {
      best->next->prev = best;  // ...then set that allocated block's 'prev' pointer to point back to our best-fit block
    }

    best->allocated = true; // mark our best-fit block as allocated
    
    new_block_ptr   = HEADER_TO_BLOCK(best); // our new block pointer will point to our new best-fit block

  /****************************************
   * If we have NOT found a best-fit block,
   *   then allocate a new block from the heap
   ***************************************/
  } else {
    
    int padding = 0; // initialize padding variable

    // find padding size
    if((sizeof(header_s) + free_addr) % 16 !=0) {
      padding = 16 - ((sizeof(header_s) + free_addr) % 16);
    }

    free_addr = free_addr + padding; // pad free address pointer
    
    header_s* header_ptr = (header_s*)free_addr;  // our new block header will begin at free_addr
    new_block_ptr = HEADER_TO_BLOCK(header_ptr);  // our pointer to the beginnign of our block

     /****************************************
     * Add our new block to the allocated 
     *   block list
     ***************************************/

    header_ptr->next      = alloc_list_head;  // set our new block's 'next' pointer to point to first element in the allocated block list
    alloc_list_head       = header_ptr; // our allocated block list pointer will now point to this block
    header_ptr->prev      = NULL;  // set our new block's 'prev' pointer to null, as this will be our first block in the allocated block list
    header_ptr->size      = size;  // set the size value to our requested block size
    header_ptr->allocated = true;  // mark this block as allocated

    // if there was already a block in the allocated block list
    if (header_ptr->next != NULL) {
      header_ptr->next->prev = header_ptr;  // ...then set that allocated block's 'prev' pointer to point back to our best-fit block
    }
    
    intptr_t new_free_addr = (intptr_t)new_block_ptr + size; // update the new free address pointer

    // if the new block goes beyond our heap, return NULL
    if (new_free_addr > end_addr) {

      return NULL;

    } else {

      // if our block allocation is within the heap, update free_addr pointer
      free_addr = new_free_addr;

    }

  }

  return new_block_ptr; // return the pointer to new memory block

} // malloc()
// ==============================================================================



// ==============================================================================
/**
 * Deallocate a given block on the heap.  Add the given block (if any) to the
 * free list.
 *
 * \param ptr A pointer to the block to be deallocated.
 */
void free (void* ptr) {

  // return if ptr is NULL
  if (ptr == NULL) {
    return;
  }

  header_s* header_ptr = BLOCK_TO_HEADER(ptr); // will hold address of current block's header

  // if the current block is not allocated, then there is an error (it is already free)
  if (!header_ptr->allocated) {
    ERROR("Double-free: ", (intptr_t)header_ptr);
  }

  /****************************************
   * Remove our block from the allocated 
   * block list
   ****************************************/

  // if our current block was the first block in our allocated list
  if (header_ptr->prev == NULL) {
    alloc_list_head = header_ptr->next;  // ...then make the next allocated block the first element in our allocated block list
  } else {
    header_ptr->prev->next = header_ptr->next; // ...otherwise, the previous allocated block's 'next' pointer will point to our current block's 'next' allocated block address
  }

  // if our current block  not the last block in the allocated block list
  if (header_ptr->next != NULL) {
    header_ptr->next->prev = header_ptr->prev; // ...then the next allocated block's 'prev' pointer will point to the previous allocated block
  }
  
  /****************************************
   * Add our block to the free block list
   ***************************************/
  
  header_ptr->next = free_list_head; // set our current block's 'next' pointer to point to first element in the free block list
  free_list_head   = header_ptr;  // our free block list pointer will now point to our current block
  header_ptr->prev = NULL; // set our current block's 'prev' point to null, as it will be the first item in the free block list

  //  if there was already a block in the free block list
  if (header_ptr->next != NULL) {
    header_ptr->next->prev = header_ptr;  // ...then set that free block's 'prev' pointer to point back to our current block
  }
  header_ptr->allocated = false; // mark current block as free

} // free()
// ==============================================================================



// ==============================================================================
/**
 * Allocate a block of `nmemb * size` bytes on the heap, zeroing its contents.
 *
 * \param nmemb The number of elements in the new block.
 * \param size  The size, in bytes, of each of the `nmemb` elements.
 * \return      A pointer to the newly allocated and zeroed block, if successful;
 *              `NULL` if unsuccessful.
 */
void* calloc (size_t nmemb, size_t size) {

  // Allocate a block of the requested size.
  size_t block_size    = nmemb * size;
  void*  new_block_ptr = malloc(block_size);

  // If the allocation succeeded, clear the entire block.
  if (new_block_ptr != NULL) {
    memset(new_block_ptr, 0, block_size);
  }

  return new_block_ptr;
  
} // calloc ()
// ==============================================================================



// ==============================================================================
/**
 * Update the given block at `ptr` to take on the given `size`.  Here, if `size`
 * fits within the given block, then the block is returned unchanged.  If the
 * `size` is an increase for the block, then a new and larger block is
 * allocated, and the data from the old block is copied, the old block freed,
 * and the new block returned.
 *
 * \param ptr  The block to be assigned a new size.
 * \param size The new size that the block should assume.
 * \return     A pointer to the resultant block, which may be `ptr` itself, or
 *             may be a newly allocated block.
 */
void* realloc (void* ptr, size_t size) {

  // Special case: If there is no original block, then just allocate the new one
  // of the given size.
  if (ptr == NULL) {
    return malloc(size);
  }

  // Special case: If the new size is 0, that's tantamount to freeing the block.
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  // Get the current block size from its header.
  header_s* header_ptr = BLOCK_TO_HEADER(ptr);

  // If the new size isn't an increase, then just return the original block as-is.
  if (size <= header_ptr->size) {
    return ptr;
  }

  // The new size is an increase.  Allocate the new, larger block, copy the
  // contents of the old into it, and free the old.
  void* new_block_ptr = malloc(size);
  if (new_block_ptr != NULL) {
    memcpy(new_block_ptr, ptr, header_ptr->size);
    free(ptr);
  }
    
  return new_block_ptr;
  
} // realloc()
// ==============================================================================

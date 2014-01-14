#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Initializes the swap table and prepares
 * the swap block device.
 */
void swap_init(void);

/**
 * Swaps a page in from swap space.
 */
void swap_in(void* page_address, int slot_number);

/**
 * Swaps a page out to swap space.
 */
int swap_out(void* page_address);

#endif // VM_SWAP_H

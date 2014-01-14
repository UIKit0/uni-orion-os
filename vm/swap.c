#include "swap.h"

#include "bitmap.h"
#include "devices/block.h"
#include "threads/vaddr.h"

/* Table used to identify free swap slots */
static struct bitmap *swap_table;

/* Block device for storing the swap slots */
static struct block *swap_block;

/**
 * Block devices work by writing and reading sectors.
 * Usually these sectors are around 512 bytes in size, so
 * more than one sector is required to hold a page.
 */
static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

void swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL) {
        PANIC("No swap block found, could not initialize swaping!");
    }
    block_sector_t swap_size = block_size(swap_block) * BLOCK_SECTOR_SIZE;
    swap_table = bitmap_create(swap_size / PGSIZE);
}

void swap_in(void* page_address, int slot_number)
{
    // calculate the sector where the page starts
    block_sector_t slot_sector = slot_number * SECTORS_PER_PAGE;

    // read the page into main memory
    size_t sector_id;
    for (sector_id = 0; sector_id < SECTORS_PER_PAGE; ++sector_id) {
        block_read(swap_block, slot_sector + sector_id, page_address + BLOCK_SECTOR_SIZE * sector_id);
    }

    // mark the swap slot as empty
    bitmap_reset(swap_table, slot_number);
}

int swap_out(void* page_address)
{
    // find an empty swap slot
    size_t slot_number = bitmap_scan_and_flip(swap_table, 0, 1, false);

    if (slot_number == BITMAP_ERROR) {
        return -1;
    }

    block_sector_t slot_sector = slot_number * SECTORS_PER_PAGE;

    // write the page to the swap space
    size_t sector_id;
    for (sector_id = 0; sector_id < SECTORS_PER_PAGE; ++sector_id) {
        block_write(swap_block, slot_sector + sector_id, page_address + BLOCK_SECTOR_SIZE * sector_id);
    }

    return slot_number;
}

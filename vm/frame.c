#include "vaddr.h"
#include "frame.h"
#include "synch.h"

static struct hash frame_table;
struct lock ft_lock;

size_t get_frame_table_size()
{
	return PGSIZE * frame_table.elem_cnt;
}

/*
 * Initializes the frame table
 */
void ft_init()
{
	hash_init(&frame_table);
}

/**
 * Adds a page to the frame table
 */
void ft_add_page(void *phys_add, void *page)
{
	lock_acquire(&ft_lock);
	// insert page in frame_table hash
	lock_release(&ft_lock);
}

/**
 * Deletes a page from the frame_table
 */
void ft_evict_page(void *phys_add)
{
	lock_acquire(&ft_lock);
	// delete page from frame_table hash
	lock_release(&ft_lock);
}

/**
 * Search in the frame table by page
 */
void* ft_search_page(void *page)
{
	lock_acquire(&ft_lock);
	// search page in frame_table hash
	lock_release(&ft_lock);
	return NULL;
}

/**
 * Returns the page corresponding
 * to phys_add
 */

void* ft_get_page(void *phys_add)
{
	void *page = NULL;
	lock_acquire(&ft_lock);
	//page = hash_table[phys_add];
	lock_release(&ft_lock);
	return page;
}

frame ft_get_unused_frame()
{
	//eviction algorithm
}

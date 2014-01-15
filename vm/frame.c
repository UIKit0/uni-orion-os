#include "vm/frame.h"

#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/swap.h"

#include <string.h>

/* The frame table */
static struct list frame_table;

/* A lock for frame_table synchronized access*/
struct lock ft_lock;

static frame* frame_lookup (void *kpage)
{
	struct list_elem *e = list_head (&frame_table);
	while ((e = list_next (e)) != list_end (&frame_table))
	{
		frame *f = list_entry(e, frame, list_elem);
		if(f->kpage == kpage)
		{
			return f;
		}
	}
	return NULL;
}

/**
 * Initializes the frame table
 */
void ft_init(void)
{
	list_init(&frame_table);
	lock_init(&ft_lock);
}

/**
 * Insert a frame into the frame table
 */
void ft_insert_frame(frame *f)
{
	f->pinned = true;
	lock_acquire(&ft_lock);
	list_push_back(&frame_table, &(f->list_elem));
	lock_release(&ft_lock);
	f->pinned = false;
}

/**
 * Removes the frame given from the
 * frame_table and frees the space
 * occupied by the frame table entry.
 */
void ft_remove_frame(frame* frame)
{
	frame->pinned = true;
	lock_acquire(&ft_lock);
	list_remove(&(frame->list_elem));
	lock_release(&ft_lock);
	free(frame);
}

/**
 * Allocates a page within a frame
 * and returns the allocated frame
 * if there is an unused frame.
 * Otherwise, it tries to evict a
 * frame and return it. If no frame
 * is unused and no frame can be evicted
 * it returns NULL. 
 */
frame* ft_alloc_frame(bool zero_page, void *page_u_addr)
{
	enum palloc_flags flags = PAL_USER | (zero_page ? PAL_ZERO : 0);

	void *page_k_addr = palloc_get_page(flags);
	if (page_k_addr != NULL ) {
		frame *f = (frame *) malloc(sizeof(frame));
		f->kpage = page_k_addr;
		f->upage = page_u_addr;
		f->pinned = false;
		ft_insert_frame(f);
		return f;
	} else {
		frame *lru_f = ft_get_lru_frame();
		if (lru_f != NULL )
		{
			lru_f->pinned = true;

			if(!ft_evict_frame(lru_f))
			{
				return NULL;
			}

			lru_f->upage = page_u_addr;
			if (zero_page)
				memset(lru_f->kpage, 0, PGSIZE );

			lru_f->pinned = false;
		}

		return lru_f;
	}

	return NULL ; //not reachable
}

void ft_free_frame(frame *f)
{
	pagedir_clear_page(thread_current()->pagedir, f->upage);
	palloc_free_page(f->kpage);
	ft_remove_frame(f);
}

/**
 * Finds the least recently used frame
 */
frame *ft_get_lru_frame(void)
{
	// First chance
	struct list_elem *e = list_head (&frame_table);
	while ((e = list_next (e)) != list_end (&frame_table))
	{
		frame *f = list_entry(e, frame, list_elem);
		if (!pagedir_is_accessed(thread_current()->pagedir, f->upage) && !f->pinned) {
			return f;
		} else {
			pagedir_set_accessed(thread_current()->pagedir, f->upage, false);
		}
	}

	// Second chance
	e = list_next(list_head(&frame_table));
	while (list_entry(e, frame, list_elem)->pinned) {
		e = list_next(e);
	}

	return list_entry(e, frame, list_elem);
}

/**
 * Tries to evict the frame given. If the
 * page within the frame was modified, the page
 * is written to swap. If dealloc is requested,
 * the memory is freed, otherwise, the memory is
 * kept for another page to be used. Returns true
 * if eviction was successful, false otherwise.
 */
bool ft_evict_frame(frame* frame)
{
	struct thread *t = thread_current();

	struct supl_pte *pte = supl_pt_get_spte(process_current(), frame->upage);
	if( pagedir_is_dirty(t->pagedir, frame->upage) || pte->page_read_bytes == 0 )
	{
		//frame->pinned = true;
		pte->swap_slot_no = swap_out(frame->kpage);
		//frame->pinned = false;

		if (pte->swap_slot_no < 0)
		{
			return false;
		}
	}

	pagedir_clear_page(t->pagedir, frame->upage);

	return true;
}

/**
 * Pins the frame, namely disqualifies it from
 * being evicted by the LRU algorithm
 */
void ft_pin_frame(const void *uaddr)
{
	void *kpage = pagedir_get_page(thread_current()->pagedir, uaddr);
	frame *f = frame_lookup(kpage);
	f->pinned = false;
}

/**
 * Unpins the frame, making it available to
 * be evicted by the LRU algorithm
 */
void ft_unpin_frame(const void *uaddr)
{
	void *kpage = pagedir_get_page(thread_current()->pagedir, uaddr);
	frame *f = frame_lookup(kpage);
	f->pinned = false;
}

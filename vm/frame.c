#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"

#include <string.h>

/* The frametable */
static struct hash frame_table;

/* A lock for frame_table synch access*/
struct lock ft_lock;

frame* frame_lookup (const void *kpage);
static bool frame_less (struct hash_elem *a_, struct hash_elem *b_, void *aux UNUSED);
static unsigned frame_hash (struct hash_elem *f_, void *aux UNUSED);

/*hash function for the frame table.
It should be computed using the kpage field
of the frame */
static unsigned frame_hash (struct hash_elem *f_, void *aux UNUSED)
{
	frame *f = hash_entry (f_, frame, he);
	return hash_int ((int)f->kpage);
}

static bool frame_less (struct hash_elem *a_, struct hash_elem *b_, void *aux UNUSED)
{
	frame *a = hash_entry (a_, frame, he);
	frame *b = hash_entry (b_, frame, he);

	return a->kpage < b->kpage;
}

frame* frame_lookup (const void *kpage)
{
	frame f;
	struct hash_elem *e;

	f.kpage = kpage;
	e = hash_find((&frame_table), &f.he);

	return e != NULL ? hash_entry (e, frame, he) : NULL ;
}

/*
 * Initializes the frame table
 */
void ft_init(void)
{
	hash_init(&frame_table, &frame_hash, frame_less, NULL);
	lock_init(&ft_lock);
}

/**
 * Insert a frame into the frame table
 */
void ft_insert_frame(frame *f)
{
	f->pinned = true;
	lock_acquire(&ft_lock);
	hash_insert(&frame_table, &(f->he));
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
	hash_delete(&frame_table, &(frame->he));
	lock_release(&ft_lock);
	free(frame);
}

/*allocates a page within a frame
  and returns the allocated frame
  if there is an unused frame.
  Otherwise, it tries to evict a
  frame and return it. If no frame
  is unused and no frame can be evicted
  it returns NULL. */
frame* 	ft_alloc_frame(bool zero_page, const void *page_u_addr)
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
				memset(lru_f->kpage, 0, 1);

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

/*
  Finds the least recently used frame
*/
frame* ft_get_lru_frame(void)
{
	//TODO: implement clock-algorithm or second-chance
	return NULL;
}

/* Tries to evict the frame given. If the
 * page within the frame was modified, the page
 * is written to swap. If dealloc is requested,
 * the memory is freed, otherwise, the memory is
 * kept for another page to be used. Returns true
 * if eviction was successful, false otherwise.
*/
bool ft_evict_frame(frame* frame)
{
	struct thread *t = thread_current();

	if( pagedir_is_dirty(t->pagedir, frame->upage) )
	{
		frame->pinned = true;
		//TODO:swap out the page
		//supl_pte = get_supl_pte(current_process, frame.upage);
		//supl_pte.slot_no = swap_out(frame.kpage)
		frame->pinned = false;
		//if(supl_pte.slot_no < 0)
			//	return false
	}

	pagedir_clear_page(t->pagedir, frame->upage);

	return true;
}

/*
 * Pins the frame, namely disqualifies it from
 * being evicted by the LRU algorithm
 */
void ft_pin_frame(const void *uaddr)
{
	void *kpage = pagedir_get_page(thread_current()->pagedir, uaddr);
	frame *f = frame_lookup(kpage);
	f->pinned = false;
}

/*
 * Unpins the frame, making it available to
 * be evicted by the LRU algorithm
 */
void ft_unpin_frame(const void *uaddr)
{
	void *kpage = pagedir_get_page(thread_current()->pagedir, uaddr);
	frame *f = frame_lookup(kpage);
	f->pinned = false;
}

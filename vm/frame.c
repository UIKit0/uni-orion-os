#include "vm/frame.h"

#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/mmap.h"

#include <string.h>

/* The frame table */
static struct list frame_table;
static struct list_elem *lru_cursor = NULL;

/* A lock for frame_table synchronized access*/
struct lock ft_lock;

static bool install_frame (frame* frame, bool writable);

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

void* ft_get_frame(void *kpage)
{
	lock_acquire(&ft_lock);
	frame *f = frame_lookup(kpage);
	lock_release(&ft_lock);
	return f;
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
	ASSERT(ft_get_frame(f->kpage) == NULL);

	lock_acquire(&ft_lock);
	list_push_back(&frame_table, &(f->list_elem));
	lock_release(&ft_lock);
}

/**
 * Removes the frame given from the
 * frame_table and frees the space
 * occupied by the frame table entry.
 * Frame needs to be pinned when
 * calling this function
 */
void ft_remove_frame(frame* toRemoveFrame)
{
	ASSERT(toRemoveFrame->pinned == true);

	lock_acquire(&ft_lock);

	frame *lru_cursor_frame = list_entry(lru_cursor, frame, list_elem);
	if(toRemoveFrame == lru_cursor_frame) {
		lru_cursor = list_next(lru_cursor);
	}
	list_remove(&(toRemoveFrame->list_elem));
	free(toRemoveFrame);
	lock_release(&ft_lock);
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
frame* ft_alloc_frame(bool zero_page, bool writable, void *page_u_addr)
{

	ASSERT(is_user_vaddr(page_u_addr));

	enum palloc_flags flags = PAL_USER | (zero_page ? PAL_ZERO : 0);

	void *page_k_addr = palloc_get_page(flags);
	frame *f;

	if (page_k_addr != NULL ) {
		f = (frame *) malloc(sizeof(frame));
		f->kpage = page_k_addr;
		f->pinned = true;
		ft_insert_frame(f);
	} else {
		f = ft_get_lru_frame();

		//for debugging assert
		//check if frame table info is consistent with page table info
		ASSERT(pagedir_get_page(f->pagedir, f->upage) == f->kpage);

		if(f == NULL)
			return NULL;

		if(!ft_evict_frame(f))
		{
			return NULL;
		}

		//egocentric programming: i have the frame, now you can die :))
		lock_release(&(f->process->shared_res_lock));


		if (zero_page)
			memset(f->kpage, 0, PGSIZE );

	}

	f->upage = page_u_addr;
	f->pagedir = thread_current()->pagedir;
	f->process = process_current();

	if (!install_frame(f, writable))
	{
		void *kpage = f->kpage;
		ft_remove_frame(f);
		palloc_free_page(kpage);
		return NULL;
	}

	f->pinned = false;

	return f;
}

void ft_free_frame(frame *f)
{
	ASSERT(f->pinned == true);

	pagedir_clear_page(f->pagedir, f->upage);
	void *kpage = f->kpage;
	ft_remove_frame(f);
	palloc_free_page(kpage);
}

/**
 * Finds the least recently used frame
 * and pins it for eviction
 */
frame *ft_get_lru_frame(void)
{
	lock_acquire(&ft_lock);

	if (lru_cursor == NULL) {
		lru_cursor = list_head(&frame_table);
	}

	while (true)
	{
		lru_cursor = list_next(lru_cursor);

		// restart from the beginning
		if (lru_cursor == list_end(&frame_table)) {
			lru_cursor = list_next(list_head(&frame_table));
		}

		frame *f = list_entry(lru_cursor, frame, list_elem);
		if (!pagedir_is_accessed(f->pagedir, f->upage) && !f->pinned) {
			//empathy programming - if the process is dying, leave him alone :)
			if(!lock_try_acquire(&f->process->shared_res_lock))
				continue;
			f->pinned = true;
			lock_release(&ft_lock);
			return f;
		} else {
			pagedir_set_accessed(f->pagedir, f->upage, false);
		}
	}
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
	ASSERT(frame->pinned == true);

	pagedir_clear_page(frame->pagedir, frame->upage);
	bool dirty = pagedir_is_dirty(frame->pagedir, frame->upage);

	struct supl_pte *pte = supl_pt_get_spte(frame->process, frame->upage);

	if(pte->swap_slot_no == -2) {

		mapped_file *mfile = NULL;
		if(dirty) {
			mfile = get_mapped_file_from_page_pointer(frame->process, frame->upage);
		}
		ASSERT(lock_held_by_current_thread(&frame->process->shared_res_lock));
		ASSERT(mfile != NULL || !dirty);

		if(mfile != NULL) {
			save_page_mm(mfile->fd, frame->upage - mfile->user_provided_location, frame->kpage);
		}
	}
	else if( dirty || pte->page_read_bytes == 0 )
	{
		pte->swap_slot_no = swap_out(frame->kpage);

		if (pte->swap_slot_no < 0)
		{
			return false;
		}
	}

	return true;
}

/**
 * This function pins a frame atomically.
 */
bool ft_atomic_pin_frame(frame *f)
{
	while(f->pinned == true);

	lock_acquire(&ft_lock);
	ASSERT(f->pinned != true);
	f->pinned = true;
	lock_release(&ft_lock);

	return true;
}

/**
 * This function pins a frame atomically.
 */
frame* ft_atomic_pin_upage(void *pagedir, void *uaddr)
{
	lock_acquire(&ft_lock);

	void *kpage = pagedir_get_page(pagedir, uaddr);

	//page is not present in page table anymore
	//evicted or in process of eviction
	if(!kpage) {
		lock_release(&ft_lock);
		return NULL;
	}

	frame *f = frame_lookup(kpage);
	if(!f)
		return NULL;

	//if frame is already pinned than page
	//might be in process of eviction but the page
	//was not set not present yet
	if(f->pinned == true)
	{
		lock_release(&ft_lock);
		return NULL;
	}

	ASSERT(f->pinned != true);
	f->pinned = true;
	lock_release(&ft_lock);

	return f;
}

/**
 * Unpins the frame, making it available to
 * be evicted by the LRU algorithm
 */
void ft_unpin_frame(frame *f)
{
	f->pinned = false;
}

void ft_unpin_frames(frame** frames, int nr_of_frames)
{
	int i;
	for(i = 0; i < nr_of_frames; ++i)
	{
		ft_unpin_frame(frames[i]);
	}
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_frame (frame *f, bool writable)
{
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (f->pagedir, f->upage) == NULL
          && pagedir_set_page (f->pagedir, f->upage, f->kpage, writable));
}



#ifndef FRAME_H
#define FRAME_H

#include <list.h>
#include "vm/common.h"

struct frame
{
	//kernel virtual address of page
	void *kpage;

	//user virtual address of page
	void *upage;

	//if frame is pinned, the contained page can not be evicted
	bool  pinned;

	struct list_elem list_elem; //needed for hash_table
};

//initializes the frame-table
void	ft_init(void);
void 	ft_insert_frame(frame *f);
void 	ft_remove_frame(frame *f);
frame* 	ft_alloc_frame(bool zero_page, void *upage);
void 	ft_free_frame(frame *f);
frame* 	ft_get_lru_frame(void);
bool 	ft_evict_frame(frame* frame);
void 	ft_pin_frame(const void *uaddr);
void 	ft_unpin_frame(const void *uaddr);
void 	ft_free_frame(frame *f);

#endif

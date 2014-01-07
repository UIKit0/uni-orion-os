#ifndef FRAME_H
#define FRAME_H

#include <hash.h>

struct frame
{
	//kernel virtual address of page
	void *kpage;

	//user virtual address of page
	void *upage;

	//if frame is pinned, the contained page can not be evicted
	bool  pinned;

	struct hash_elem he; //needed for hash_table
};

typedef struct frame frame;

//initializes the frame-table
void	ft_init(void);
void 	ft_insert_frame(frame *f);
void 	ft_remove_frame(frame *f);
frame* 	ft_alloc_frame(bool zero_page, const void *upage);
frame* 	ft_get_lru_frame(void);
bool 	ft_evict_frame(frame* frame, bool dealloc_page);
void 	ft_pin_frame(const void *uaddr);
void 	ft_unpin_frame(const void *uaddr);

//size_t ft_get_size(void);
//void   ft_add_page(void *phys_add, void *page);
//void   ft_evict_page(void *phys_add);
//void*  ft_search_page(void *page);
//void*  ft_get_page(void *phys_add);
//frame*  ft_get_unused_frame(void);


#endif

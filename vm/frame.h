#ifndef FRAME_H
#define FRAME_H

#include "hash.h"

struct frame
{
	void *phys_add; //physical address - key in hash_t
	void *page_add;
	bool  pinned;
	struct hash_elem he;
};

typedef struct frame frame;

size_t ft_get_size();
void   ft_init();
void   ft_add_page(void *phys_add, void *page);
void   ft_evict_page(void *phys_add);
void*  ft_search_page(void *page);
void*  ft_get_page(void *phys_add);
frame  ft_get_unused_frame();


#endif

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include "filesys/file.h"
#include <hash.h>
#include "userprog/common.h"
#include "vm/common.h"

struct supl_pte {						// an entry (element) in the supplemental page table
	uint32_t	virt_page_no; 			// the number of the virtual page (also the hash key) having info about
	void*		virt_page_addr;			// the virtual address of the page
	off_t 		ofs;					// the offset in file the bytes to be stored in the page must be read from
	size_t 		page_read_bytes;		// number of bytes to read from the file and store in the page
	size_t 		page_zero_bytes; 		// number of bytes to zero in the page
	bool		writable;				// indicate if the page is writable or not
	int 		swap_slot_no;			// if >=0, the slot_no where the page is in swap, otherwise page is not in swap
	struct hash_elem he;				// element to insert the structure in a hash list
};

void supl_pt_init(struct hash *);
void supl_pt_free(struct hash *);
void supl_pt_insert(struct hash *spt, supl_pte *spte);
void supl_pt_remove(struct hash *spt, supl_pte *spte);
supl_pte* supl_pt_get_spte(process_t *p, void *uaddr);


#endif

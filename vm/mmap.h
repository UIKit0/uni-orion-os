/*
 * mmap.h
 *
 *  Created on: Feb 3, 2014
 *      Author: andrei
 */

#ifndef MMAP_H_
#define MMAP_H_

#include "userprog/common.h"
#include "vm/common.h"
#include <list.h>

typedef int mapid_t;
struct mapped_file {
	mapid_t id;
	struct file *fd;
	int fd2;
	void *user_provided_location;
	size_t file_size;
	struct list_elem lst;
};

mapped_file*	  mfd_get_file(int mfd);
struct list_elem* mf_remove_file(mapped_file *mf);
mapid_t		 	  mfd_create(void);
mapped_file*	  get_mapped_file_from_page_pointer(process_t *p, void *pagePointer);
void 			  munmap_all(void);
struct list_elem* mummap_wrapped(mapped_file *fl);


#endif /* MMAP_H_ */

#include "vm/mmap.h"

#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/file.h"
#include "filesys/fd.h"

mapped_file *mfd_get_file(int mfd)
{
	struct list* file_descriptors = &process_current()->mmap_list;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		mapped_file *link = list_entry(e, mapped_file, lst);
		if (link->id == mfd) {
			return link;
		}
	}
	return NULL;
}

struct list_elem* mf_remove_file(mapped_file *mf)
{
	if(mf == NULL)
		return NULL;

	struct list_elem *e = list_remove(&mf->lst);
	free(mf);
	return e;
}


mapid_t mfd_create(void) {
	struct list* file_descriptors = &process_current()->mmap_list;
	struct list_elem *e;
	mapid_t max_fd = 2;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		int fd = list_entry(e, mapped_file, lst)->id;
		if (fd > max_fd) {
			max_fd = fd;
		}
	}
	return max_fd + 1;
}

mapped_file *get_mapped_file_from_page_pointer(process_t *p, void *pagePointer) {
	struct list* file_descriptors = &(p->mmap_list);
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)) {
		mapped_file *mmentry = list_entry(e, mapped_file, lst);
		if(mmentry->user_provided_location <= pagePointer &&
			(pagePointer <= mmentry->user_provided_location + mmentry->file_size))
				return mmentry;
	}

	return NULL;
}

struct list_elem* mummap_wrapped(mapped_file *fl) {
	if(fl == NULL) {
		return NULL;
	}
	struct file* cfile = fl->fd;

	if(cfile == NULL) {
		return mf_remove_file(fl);
	}
	char *addr = (char *)fl->user_provided_location;
	uint32_t *pd = thread_current()->pagedir;
	process_t *pr_crt = process_current();

	while(addr < (char*)fl->user_provided_location + fl->file_size) {

		void *kpage = pagedir_get_page (pd, addr);
		if(kpage)
		{
			void *frame = ft_get_frame(kpage);
			if( frame != NULL && ft_atomic_pin_frame(frame))
			{
				lock_acquire(&pr_crt->shared_res_lock);
				ft_evict_frame(frame);
				lock_release(&pr_crt->shared_res_lock);
				ft_free_frame(frame);
			}
		}
		/*if(kpage && pagedir_is_dirty(pd, addr)) {
			save_page_mm(fl->fd, addr - (char*)fl->user_provided_location, kpage);
		}*/
		supl_pt_remove_spte(pr_crt, addr);
		addr += PGSIZE;
	}
	struct fd_list_link* link = fd_get_link(fl->fd2);

	if(!link) {
		filesys_lock();
		file_close(fl->fd);
		filesys_unlock();
	}
	else {
		link->mapped = false;
	}

	lock_acquire(&pr_crt->shared_res_lock);
	struct list_elem* e = mf_remove_file(fl);
	lock_release(&pr_crt->shared_res_lock);
	return e;
}

void munmap_all(void) {
	process_t *cp = process_current();
	struct list* file_descriptors = &cp->mmap_list;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors);) {
		mapped_file *mmentry = list_entry(e, mapped_file, lst);
		e = mummap_wrapped(mmentry);
	}
}

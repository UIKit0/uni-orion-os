#include "filesys/fd.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"

/*
 *	Creates a new, unused, file descriptor.
 */
 int fd_create(void) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	int max_fd = 2;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		int fd = list_entry(e, struct fd_list_link, l_elem)->fd;
		if (fd > max_fd) {
			max_fd = fd;
		}
	}
	return max_fd + 1;
}

/*
 * Returns true if the file descriptor was assigned to a file opened by this process.
 */
bool fd_is_valid(int fd, int direction) {
	if (fd == STDOUT && (direction & WRITE) != 0) {
		return true;
	}

	if (fd == STDIN && (direction & READ) != 0) {
		return true;
	}

	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		int current_fd = list_entry(e, struct fd_list_link, l_elem)->fd;
		if (current_fd == fd) {
			return true;
		}
	}
    return false;
}

/*
 *	Returns the file structured managed by this file descriptor.
 */
struct file *fd_get_file(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
		if (link->fd == fd) {
			return link->file;
		}
	}
	return NULL;
}

/*
 *	Returns the list element that links this file descriptor;
 */
struct fd_list_link *fd_get_link(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
		if (link->fd == fd) {
			return link;
		}
	}
	return NULL;
}

void fd_remove_file(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); ){
		struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
		e = list_next(e);
		if (link->fd == fd) {
			list_remove(&link->l_elem);
			free(link);
			return;
		}
	}
}




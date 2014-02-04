#ifndef FILESYS_FD_H
#define FILESYS_FD_H

#include <list.h>
#include <stdio.h>

#include "userprog/common.h"
#include "filesys/file.h"
#include "filesys/directory.h"

#define STDIN 	0
#define STDOUT 	1

#define READ	1
#define WRITE 	2

struct fd_list_link {
	struct list_elem l_elem;
	int fd;
#ifdef FILESYS_SUBDIRS
	bool is_directory;
	union {
		struct file *file;
		struct dir *dir;
	}
#endif
	bool mapped;
};

int    	fd_create(void);
bool   	fd_is_valid(int fd, int direction);
struct 	file* fd_get_file(int fd);
struct 	fd_list_link *fd_get_link(int fd);
void   	fd_close(int fd);

#ifdef FILESYS_SUBDIRS
bool	fd_is_directory(int fd);
int		fd_inode_number(int fd);
#endif

#endif /* FD_H_ */

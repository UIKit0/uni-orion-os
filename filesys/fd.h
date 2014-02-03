/*
 * fd.h
 *
 *  Created on: Feb 3, 2014
 *      Author: andrei
 */

#ifndef FD_H_
#define FD_H_

#include "userprog/common.h"
#include <list.h>
#include <stdio.h>

#define STDIN 	0
#define STDOUT 	1

#define READ	1
#define WRITE 	2

struct fd_list_link {
	struct list_elem l_elem;
	int fd;
	struct file *file;
	bool mapped;
};

int    fd_create(void);
bool   fd_is_valid(int fd, int direction);
struct file* fd_get_file(int fd);
struct fd_list_link *fd_get_link(int fd);
void   fd_remove_file(int fd);


#endif /* FD_H_ */

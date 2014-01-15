#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define STDIN 	0
#define STDOUT 	1

struct mapped_file;


void syscall_init (void);
struct mapped_file *get_mapped_file_from_page_pointer(void *pagePointer);
void munmap_all(void);


#endif /* userprog/syscall.h */

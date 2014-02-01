#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define STDIN 	0
#define STDOUT 	1

#ifdef VM
struct mapped_file;
#endif

struct file;


struct file *fd_get_file(int fd);
void syscall_init (void);

#ifdef VM
struct mapped_file *get_mapped_file_from_page_pointer(void *pagePointer);
void munmap_all(void);
#endif


#endif /* userprog/syscall.h */

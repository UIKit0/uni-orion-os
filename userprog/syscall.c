#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"

#define READ	1
#define WRITE 	2

static void syscall_handler (struct intr_frame *);

static void syscall_halt(struct intr_frame *f);
static void syscall_exit(struct intr_frame *f);
static void syscall_exec(struct intr_frame *f);
static void syscall_wait(struct intr_frame *f);
static void syscall_create(struct intr_frame *f);
static void syscall_remove(struct intr_frame *f);
static void syscall_open(struct intr_frame *f);
static void syscall_filesize(struct intr_frame *f);
static void syscall_read(struct intr_frame *f);
static void syscall_write(struct intr_frame *f);
static void syscall_seek(struct intr_frame *f);
static void syscall_tell(struct intr_frame *f);
static void syscall_close(struct intr_frame *f);

/* Initializes the syscall handler. */
void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Verify a given address is safe for read */
static bool is_valid_user_address_for_read(char* address UNUSED) {
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*address));
	return result != -1;
}


/* Verify a given address is safe for write */
static bool is_valid_user_address_for_write(char* address UNUSED) {
	uint8_t byte = 0;
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
	: "=&a" (error_code), "=m" (*address) : "q" (byte));
	return error_code != -1;
}

/* Verify a given address range belongs to safe user space. */
static bool is_valid_user_string_read(char* address UNUSED) {
	char *p;
	for(p = address;; ++p) {
		if(is_user_vaddr(p) && is_valid_user_address_for_read(p)) {
			if(*p == 0)			
				return true;
		}
		else {
			return false;
		}
	}
	return false;
}

/* Verify a given address range belongs to safe user space. */
static bool is_valid_user_address_range_read(char* address UNUSED, int size UNUSED) {
	char *end = address + size;
	
	if(!is_user_vaddr(end) || !is_user_vaddr(address))
		return false;
	
	char *p;
	for(p = address; p < end; ++p)
		if(!is_valid_user_address_for_read(p))
			return false;

	return true;	
}

/* Verify a given address is safe for write */
static bool is_valid_user_address_range_write(char* address UNUSED, int size UNUSED) {
	char *end = address + size;

	if(!is_user_vaddr(end) || !is_user_vaddr(address))
		return false;

	char *p;
	for(p = address; p < end; ++p)
		if(!is_valid_user_address_for_write(p))
			return false;
	return true;	
}



static void syscall_halt(struct intr_frame *f) {
	f->eax = 0;
}

/*
 *	Creates a new, unused, file descriptor.
 */
static int fd_create(void) {
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
static bool fd_is_valid(int fd, int direction) {
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
static struct file *fd_get_file(int fd) {
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
static struct list_elem *fd_get_list_elem(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
    for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
    	struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
        if (link->fd == fd) {
        	return &link->l_elem;
        }
    }
    return NULL;
}

/* Slap the user and kill his process. */
static void kill_current_process(void) {
	process_current()->exit_code = -1;
	thread_exit();
}

/* Terminate this process. */
void syscall_exit(struct intr_frame *f) {
	process_current()->exit_code = ((int*)f->esp)[1];
	thread_exit();	
}

/* Wait for a child process to die. */
void syscall_wait(struct intr_frame *f) {
	f->eax = process_wait(((int*)f->esp)[1]);
}

/* Create a file. */
static void syscall_create(struct intr_frame *f) {
	char* file_name = (char*) ((int*)f->esp)[1];
	int initial_size = ((int*)f->esp)[2];

	if (!is_valid_user_string_read(file_name)) {
		kill_current_process();
		return;
	}

	bool success = filesys_create(file_name, initial_size);
	f->eax = success;
}

/* Delete a file. */
static void syscall_remove(struct intr_frame *f) {
	char *file_name = (char*) ((int*)f->esp)[1];

	if (!is_valid_user_string_read(file_name)) {
		kill_current_process();
		return;
	}

	bool success = filesys_remove(file_name);
	f->eax = success;
}

/* Open a file. */
static void syscall_open(struct intr_frame *f) {
	char *file_name = (char*) ((int*)f->esp)[1];

	if (!is_valid_user_string_read(file_name)) {
		kill_current_process();
		return;
	}

	struct file *file = filesys_open(file_name);
	if (file == NULL){
		f->eax = -1;
		return;
	}

	// get a new file_descriptor
	int fd = fd_create();

	struct fd_list_link link;
	link.fd = fd;
	link.file = file;
	list_push_back(&process_current()->owned_file_descriptors, &link.l_elem);

	f->eax = fd;
}

/* Obtain a file's size. */
static void syscall_filesize(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!fd_is_valid(fd, READ | WRITE) || fd == STDIN  || fd == STDOUT) {
		f->eax = -1;
		return;
	}

	struct file *file = fd_get_file(fd);
	f->eax = file != NULL ? file_length(file) : 0;
}

/* Read from a file. */
static void syscall_read(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	char* buffer = (char*)((int*)f->esp)[2];
	unsigned int size = (unsigned int) ((int*)f->esp)[3];

	if (!is_valid_user_address_range_write(buffer, size)) {
		kill_current_process();
		return;
	}

	if (!fd_is_valid(fd, READ)) {
		f->eax = 0;
		return;
	}

	if (fd == STDIN) {
		// TODO: read from the keyboard using input_getc()
	}

	struct file* file = fd_get_file(fd);
	f->eax = file != NULL ? file_read(file, buffer, size) : 0;
}

/* Write to a file. */
void syscall_write(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	char *buffer = (char*) ((int*)f->esp)[2];
	unsigned int size = (unsigned int) ((int*)f->esp)[3];

	if (!is_valid_user_address_range_read(buffer, size)) {
		kill_current_process();
		return;
	}
	
	if (!fd_is_valid(fd, WRITE)) {
		f->eax = 0;
		return;
	}

	if (fd == STDOUT) {
		putbuf(buffer, size);
	}

	struct file* file = fd_get_file(fd);
	f->eax = file != NULL ? file_write(file, buffer, size) : 0;
}

/* Change position in a file. */
static void syscall_seek(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	unsigned int position = ((int*)f->esp)[2];

	if (!fd_is_valid(fd, WRITE)) {
		f->eax = 0;
		return;
	}

	struct file *file = fd_get_file(fd);
	if (file != NULL) {
		file_seek(file, position);
	}
}

/* Report current position in a file. */
static void syscall_tell(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!fd_is_valid(fd, READ | WRITE)) {
		f->eax = -1;
		return;
	}

	struct file *file = fd_get_file(fd);
	f->eax = file != NULL ? file_tell(file) : 0;
}

/* Close a file. */
static void syscall_close(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!fd_is_valid(fd, READ | WRITE) || fd == STDIN || fd == STDOUT) {
		return;
	}

	file_close(fd_get_file(fd));
	list_remove(fd_get_list_elem(fd));
}

/* Start another process. */
void syscall_exec(struct intr_frame *f) {
	char *buf = (char*) ((int*)f->esp)[1];
	if (!is_valid_user_string_read(buf)) {
		kill_current_process();
		f->eax = -1;
		return;		
	}	
	f->eax = process_execute(buf);
}

static void syscall_handler (struct intr_frame *f) 
{
	if(!is_valid_user_address_range_read(f->esp, 4)) {
		kill_current_process();		
		return;
	}

  	int syscall_no = ((int*)f->esp)[0];	
	switch (syscall_no) {
		case SYS_HALT:
			syscall_halt(f);
			break;
		case SYS_EXIT:
			syscall_exit(f);
			break;
		case SYS_EXEC:
			syscall_exec(f);
			break;
		case SYS_WAIT:
			syscall_wait(f);
			break;
		case SYS_CREATE:
			syscall_create(f);
			break;
		case SYS_REMOVE:
			syscall_remove(f);
			break;
		case SYS_OPEN:
			syscall_open(f);
			break;
		case SYS_FILESIZE:
			syscall_filesize(f);
			break;
		case SYS_READ:
			syscall_read(f);
			break;
		case SYS_WRITE:
			syscall_write(f);
			break;
		case SYS_SEEK:
			syscall_seek(f);
			break;
		case SYS_TELL:
			syscall_tell(f);
			break;
		case SYS_CLOSE:
			syscall_close(f);
			break;
		default:
			thread_exit();  
		}
}

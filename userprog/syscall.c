#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"

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

/* Verify a given address belongs to safe user space. */
static bool is_valid_user_address(char* address) {
	return true;
}

/* Verify a given address range belongs to safe user space. */
static bool is_valid_user_address_range(char* address, int size) {
	return true;
}

/* 
 * Returns true if the file descriptor was assigned to a file opened by this process. 
 */
static bool is_valid_file_descriptor(int fd, int direction) {
	if (fd == STDOUT && (direction & WRITE) != 0) {
		return true;
	}

	if (fd == STDIN && (direction & READ) != 0) {
		return true;
	}

	// TODO: search through opened file descriptors
}

/* Slap the user and kill his process. */
static void kill_current_process() {
	process_current()->exit_code = 0x0BADF;
	thread_exit();	
}

/* Terminate this process. */
void syscall_exit(struct intr_frame *f) {
	process_current()->exit_code = ((int*)f->esp)[1];
	thread_exit();	
}

/* Wait for a child process to die. */
void syscall_wait(struct intr_frame *f) {
	process_wait(((int*)f->esp)[1]);
}

/* Create a file. */
static void syscall_create(struct intr_frame *f) {
	char* file_name = ((int*)f->esp)[1];
	int initial_size = ((int*)f->esp)[2];

	if (!is_valid_user_address(file_name)) {
		kill_current_process();
		return;
	}

	// TODO: returns true if successful
}

/* Delete a file. */
static void syscall_remove(struct intr_frame *f) {
	char* file_name = (char*) ((int*)f->esp)[1];

	if (!is_valid_user_address(file_name)) {
		kill_current_process();
		return;
	}

	// TODO: returns true if successful
}

/* Open a file. */
static void syscall_open(struct intr_frame *f) {
	char* file_name = (char*) ((int*)f->esp)[1];

	if (!is_valid_user_address(file_name)) {
		kill_current_process();
		return;
	}

	// TODO: returns a nonnegative file descriptor or -1 in case of error
}

/* Obtain a file's size. */
static void syscall_filesize(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!is_valid_file_descriptor(fd, READ | WRITE) || fd == STDIN  || fd == STDOUT) {
		f->eax = -1;
		return;
	}

	// TODO: returns the size in bytes
}

/* Read from a file. */
static void syscall_read(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	char* buffer = (char*)((int*)f->esp)[2];
	unsigned int size = (unsigned int) ((int*)f->esp)[3];

	if (!is_valid_user_address_range(buffer, size)) {
		kill_current_process();
		return;
	}

	if (!is_valid_file_descriptor(fd, READ)) {
		f->eax = 0;
		return;
	}

	// TODO: return the number of bytes read
}

/* Write to a file. */
void syscall_write(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	char *buffer = (char*) ((int*)f->esp)[2];
	unsigned int size = (unsigned int) ((int*)f->esp)[3];

	if (!is_valid_user_address_range(buffer, size)) {
		kill_current_process();
		return;
	}

	if (!is_valid_file_descriptor(fd, WRITE)) {
		f->eax = 0;
		return;
	}

	if (fd == STDOUT) {
		putbuf(buffer, size);
	}

	// TODO: implement!!

	f->eax = size;
}

/* Change position in a file. */
static void syscall_seek(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	unsigned int position = ((int*)f->esp)[2];

	if (!is_valid_file_descriptor(fd, WRITE)) {
		f->eax = 0;
		return;
	}

	// TODO: implement!! void return type
}

/* Report current position in a file. */
static void syscall_tell(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!is_valid_file_descriptor(fd, READ | WRITE)) {
		f->eax = -1;
		return;
	}

	// TODO: returns the position of the next byte to be read or written.
}

/* Close a file. */
static void syscall_close(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];

	if (!is_valid_file_descriptor(fd, READ | WRITE) || fd == STDIN || fd == STDOUT) {
		return;
	}

	// TODO: close the file descriptor
}

/* Start another process. */
void syscall_exec(struct intr_frame *f) {
	//check buffers here
	char *buf = (char*) ((int*)f->esp)[1];
	f->eax = process_execute(buf);
}

static void syscall_handler (struct intr_frame *f) 
{
  	int syscall_no = ((int*)f->esp)[0];	
	switch (syscall_no) {
		case SYS_HALT:
			//syscall_halt(f);
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

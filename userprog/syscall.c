#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

void syscall_halt(struct intr_frame *f UNUSED);
void syscall_exit(struct intr_frame *f UNUSED);
void syscall_exec(struct intr_frame *f UNUSED);
void syscall_wait(struct intr_frame *f UNUSED);
void syscall_create(struct intr_frame *f UNUSED);
void syscall_remove(struct intr_frame *f UNUSED);
void syscall_open(struct intr_frame *f UNUSED);
void syscall_filesize(struct intr_frame *f UNUSED);
void syscall_read(struct intr_frame *f UNUSED);
void syscall_write(struct intr_frame *f UNUSED);
void syscall_seek(struct intr_frame *f UNUSED);
void syscall_tell(struct intr_frame *f UNUSED);
void syscall_close(struct intr_frame *f UNUSED);


void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void syscall_exit(struct intr_frame *f UNUSED) {
	process_current()->exit_code = ((int*)f->esp)[1];
	thread_exit();	
}

void syscall_wait(struct intr_frame *f UNUSED) {
	process_wait(((int*)f->esp)[1]);
}

void syscall_write(struct intr_frame *f UNUSED) {
	int fd = ((int*)f->esp)[1];
	char *buf = (char*) ((int*)f->esp)[2];
	int no = ((int*)f->esp)[3];

	if (fd == 1) {
		//we trust you... You won't give bad buffers
		putbuf(buf, no);
	}
	f->eax = no;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  	int syscall_no = ((int*)f->esp)[0];	
	switch (syscall_no) {
		case SYS_EXIT:
			syscall_exit(f);
			return;
		case SYS_WAIT:
			syscall_wait(f);
			return;
		case SYS_WRITE:
			syscall_write(f);
			return;
		}
	thread_exit ();  
}

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Added by Adrian Colesa - multithreading */
	int syscall_no = ((int*)f->esp)[0];
	int fd, no;
	char *buf;	
	
	switch (syscall_no) {
		case SYS_EXIT:
			printf ("SYS_EXIT system call from thread %d!\n", thread_current()->tid);
			thread_exit();
			return;
		case SYS_WRITE:
			fd = ((int*)f->esp)[1];
			buf = (char*) ((int*)f->esp)[2];
			no = ((int*)f->esp)[3];

			if (fd == 1) {
				//we trust you... You won't break stuff.
				putbuf(buf, no);
			}
			f->eax = no;
			return;
		}
	thread_exit ();  
}

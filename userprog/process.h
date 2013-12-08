#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include <hash.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/common.h"

struct fd_entry {
	int fd;
	struct file *file;
};
typedef struct fd_entry fd_entry;

#define MAX_OPEN_FILES_PER_PROCESS 32

struct process_t {
	/* process id of this process */
	pid_t pid;
	/* pid (process id) of this process's parent */
	pid_t ppid;
	/* status of the process: one of ALIVE, KILLED, DEAD. */
	enum pstatus_t status;
	/* the code returned by the process at exit */
	int exit_code;
	/* List of file descriptors. */
	fd_entry owned_file_descriptors[MAX_OPEN_FILES_PER_PROCESS];
	/* Pointer to element in hash table */
	struct hash_elem h_elem;
	/* wating semaphore instead of thread_block/unblock hackarounds */
	struct semaphore process_semaphore;
	/*file used for denying writes */
	struct file * exe_file;
};



pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);
void process_init(void);

process_t * process_current(void);
process_t * find_process(pid_t pid);

#endif /* userprog/process.h */

#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>
#include <hash.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/common.h"


#define MAX_OPEN_FILES_PER_PROCESS 16

struct process_t {
	/* process id of this process */
	pid_t pid;
	/* pid (process id) of this process's parent */
	pid_t ppid;
	/* status of the process: one of ALIVE, KILLED, DEAD. */
	enum pstatus_t status;
	/* the code returned by the process at exit */
	int exit_code;
	/* number of opened files */
	int num_of_opened_files;
	/* List of file descriptors. */
	struct list owned_file_descriptors;
	/* Pointer to element in hash table */
	struct hash_elem h_elem;
	/* wating semaphore instead of thread_block/unblock hackarounds */
	struct semaphore process_semaphore;
	/*file used for denying writes */
	struct file * exe_file;
};

struct fd_list_link {
	struct list_elem l_elem;
	int fd;
	struct file *file;
};

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);
void process_init(void);

void filesys_lock(void);
void filesys_unlock(void);

process_t * process_current(void);
process_t * find_process(pid_t pid);

#endif /* userprog/process.h */

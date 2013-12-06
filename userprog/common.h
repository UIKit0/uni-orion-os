#ifndef USERPROG_COMMON_H
#define USERPROG_COMMON_H
#include <list.h>
#include <hash.h>
#include <threads/synch.h>

/* Process identifier type.
   You can redefine this to whatever type you like. */
typedef int pid_t;

static pid_t const PID_ERROR = -1;

/* forward declarations */
struct thread;

enum pstatus_t {
	ALIVE, 
	KILLED, 
	DEAD
};

struct process_t {
	/* process id of this process */
	pid_t pid;
	/* pid (process id) of this process's parent */
	pid_t ppid;
	/* status of the process: one of ALIVE, KILLED, DEAD. */
	enum pstatus_t status;
	/* the code returned by the process at exit */
	int exit_code;
	/* The thread that is waiting after this thread. */
	struct thread* waiter_thread;
	/* List of file descriptors. */
	struct list owned_file_descriptors;
	/* Pointer to element in hash table */
	struct hash_elem h_elem;

	/* wating sema instead of thread_block/unblock hackarounds */
	struct semaphore process_semaphore;
};
typedef struct process_t process_t;

#endif

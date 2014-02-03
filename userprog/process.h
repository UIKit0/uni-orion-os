#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/common.h"
#ifdef VM
  #include "vm/common.h"
#endif


#define MAX_OPEN_FILES_PER_PROCESS 16
#define MAX_STACK_SIZE (8 * 1024 * 1024)

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
	/*file used for denying writes
    and for lazy loading of executable*/
	struct file * exe_file;
	/*Locks the resources shared by this process with more process:like mmaps, spte's*/
	struct lock shared_res_lock;
#ifdef VM
	//supplemental page table - needed for lazy loading
	struct hash supl_pt;
	struct list mmap_list;
#endif
};

typedef int mapid_t;
struct mapped_file {
	mapid_t id;
	struct file *fd;
	int fd2;
	void *user_provided_location;
	size_t file_size;
	struct list_elem lst;
};
typedef struct mapped_file mapped_file;

struct fd_list_link {
	struct list_elem l_elem;
	int fd;	
	struct file *file;
	bool mapped;
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

#ifdef VM
  bool load_page_lazy(process_t *p, supl_pte *supl_pte);
  bool load_page_mm(struct file* fd, int ofs, uint8_t *upage);
  bool save_page_mm(struct file* fd, int ofs, uint8_t *kpage);
  bool stack_growth(int nr_of_pages);
#endif


#endif /* userprog/process.h */

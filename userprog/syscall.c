#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#ifdef VM
	#include "vm/frame.h"
	#include "vm/page.h"
#endif

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

#ifdef VM
static void syscall_mmap(struct intr_frame *f);
static void syscall_munmap(struct intr_frame *f);

static struct list_elem* mummap_wrapped(mapped_file *fl);
void prevent_page_faults(unsigned char *buffer, size_t size, frame **frames);
void unpin_frames(frame** frames, int nr_of_frames);
#endif



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
struct file *fd_get_file(int fd) {
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

#ifdef VM
static mapped_file *mfd_get_file(int mfd) {
	struct list* file_descriptors = &process_current()->mmap_list;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		mapped_file *link = list_entry(e, mapped_file, lst);
		if (link->id == mfd) {
			return link;
		}
	}
	return NULL;
}
#endif

#ifdef VM
static struct list_elem* mf_remove_file(mapped_file *mf) {
	if(mf == NULL)
		return NULL;

	struct list_elem *e = list_remove(&mf->lst);
	free(mf);
	return e;
}
#endif

#ifdef VM
static mapid_t mfd_create(void) {
	struct list* file_descriptors = &process_current()->mmap_list;
	struct list_elem *e;
	mapid_t max_fd = 2;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		int fd = list_entry(e, mapped_file, lst)->id;
		if (fd > max_fd) {
			max_fd = fd;
		}
	}
	return max_fd + 1;
}
#endif

/*
 *	Returns the list element that links this file descriptor;
 */
static struct fd_list_link *fd_get_link(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)){
		struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
		if (link->fd == fd) {
			return link;
		}
	}
	return NULL;
}

static void fd_remove_file(int fd) {
	struct list* file_descriptors = &process_current()->owned_file_descriptors;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); ){
		struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
		e = list_next(e);		
		if (link->fd == fd) {
			list_remove(&link->l_elem);
			free(link);			
			return;
		}
	}	
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
	filesys_lock();
	bool success = filesys_create(file_name, initial_size);
	filesys_unlock();
	f->eax = success;
}

/* Delete a file. */
static void syscall_remove(struct intr_frame *f) {
	char *file_name = (char*) ((int*)f->esp)[1];

	if (!is_valid_user_string_read(file_name)) {
		kill_current_process();
		return;
	}

	filesys_lock();	
	bool success = filesys_remove(file_name);
	filesys_unlock();
	f->eax = success;
}

/* Open a file. */
static void syscall_open(struct intr_frame *f) {
	char *file_name = (char*) ((int*)f->esp)[1];
	process_t *current = process_current();

	if(current->num_of_opened_files >= MAX_OPEN_FILES_PER_PROCESS) {
		f->eax = -1;
		return;
	}
	

	if (!is_valid_user_string_read(file_name)) {
		kill_current_process();
		return;
	}

	
	filesys_lock();
	struct file *file = filesys_open(file_name);
	filesys_unlock();
	if (file == NULL){
		f->eax = -1;
		return;
	}


	// get a new file_descriptor
	int fd = fd_create();

	struct fd_list_link *link = (struct fd_list_link *)malloc(sizeof(struct fd_list_link));

	if(link == NULL) {
		file_close(file);
		f->eax = -1;
		return;
	}

	link->fd = fd;
	link->file = file;
	link->mapped = false;
	list_push_back(&current->owned_file_descriptors, &(link->l_elem));
	current->num_of_opened_files++;

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
	filesys_lock();
	f->eax = file != NULL ? file_length(file) : 0;
	filesys_unlock();
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

#ifdef VM
	//make sure that every page is in memory and will not be swapped out
	int nr_of_frames = (pg_round_up(buffer + size) - pg_round_down(buffer)) / PGSIZE;
	frame** frames = (frame *)malloc(nr_of_frames * sizeof(frame*));
	prevent_page_faults(buffer, size, frames);
#endif

	filesys_lock();
	f->eax = file != NULL ? file_read(file, buffer, size) : 0;
	filesys_unlock();

#ifdef VM
	unpin_frames(frames, nr_of_frames);
#endif
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

#ifdef VM
	//make sure that every page is in memory and will not be swapped out
	int nr_of_frames = (pg_round_up(buffer + size) - pg_round_down(buffer)) / PGSIZE;
	frame** frames = (frame *)malloc(nr_of_frames * sizeof(frame*));
	prevent_page_faults(buffer, size, frames);
#endif

	filesys_lock();
	f->eax = file != NULL ? file_write(file, buffer, size) : 0;
	filesys_unlock();

#ifdef VM
	unpin_frames(frames, nr_of_frames);
#endif

}
#ifdef VM
void prevent_page_faults(unsigned char *buffer, size_t size, frame** frames)
{
	unsigned char *start = pg_round_down(buffer);
	unsigned char *end = buffer + size;
	unsigned int i = 0;

	process_t *crt_proc = process_current();
	void *pagedir = thread_current()->pagedir;

	while(start < end)
	{
		if(!pagedir_get_page(pagedir, start))
		{
			//page is not present
			supl_pte *spte = supl_pt_get_spte(crt_proc, start);
			load_page_lazy(crt_proc, spte);
		}

		frames[i] = ft_atomic_pin_upage(pagedir, start);
		if(!frames[i])
		{
			//this means that page just loaded is in eviction process
			//give up or try again
			// a better idea is a function called : load_and_pin_page
			continue; //retry - we did not increment
		}
		else
		{
			i++;
		}

		start += PGSIZE;
	}
}

void unpin_frames(frame** frames, int nr_of_frames)
{
	int i;
	for(i = 0; i < nr_of_frames; ++i)
	{
		ft_unpin_frame(frames[i]);
	}
}
#endif

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
		filesys_lock();
		file_seek(file, position);
		filesys_unlock();
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
	filesys_lock();
	f->eax = file != NULL ? file_tell(file) : 0;
	filesys_unlock();
}

/* Close a file. */
static void syscall_close(struct intr_frame *f) {
	int fd = ((int*)f->esp)[1];
	process_t *current = process_current();

	if (!fd_is_valid(fd, READ | WRITE) || fd == STDIN || fd == STDOUT) {
		return;
	}
	
	current->num_of_opened_files--;
	filesys_lock();
	struct fd_list_link *link = fd_get_link(fd);
	if(link->mapped == false) {
		file_close(link->file);
	}	
	filesys_unlock();
	fd_remove_file(fd);
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

#ifdef VM
static supl_pte* gPages[1024];
#endif

#ifdef VM
static void syscall_mmap(struct intr_frame *f) {
	f->eax = -1;
	process_t *p = process_current();
	
	int fd = (int) ((int*)f->esp)[1];
	char *addr = (void *) ((int*)f->esp)[2];
	char *addrStart = addr;

	struct file* fl = fd_get_file(fd);

	
	if(fl == NULL) {
		return;
	}

	if((int)addr % PGSIZE != 0 || addr == NULL) {
		return;
	}

	filesys_lock();
	int filesize = file_length(fl);
	filesys_unlock();

	int zeroOrOne = filesize % PGSIZE ? 1 : 0;
	int pageNumber = filesize / PGSIZE + zeroOrOne;
	int pageIndex = 0;


	struct thread *th = thread_current();

	while(pageIndex < pageNumber) {
		supl_pte *spte = (supl_pte*)malloc(sizeof(supl_pte));
		spte->virt_page_no = pg_no(addr);
		spte->virt_page_addr = addr;
		spte->swap_slot_no = -2;
		spte->writable = true;
		spte->page_read_bytes = 0;
		gPages[pageIndex] = spte;
		if(supl_pt_get_spte(p, addr) != NULL) {
			--pageIndex;
			//rollback & break
			while(pageIndex >= 0) {
				supl_pt_remove(&(p->supl_pt), gPages[pageIndex]);
				--pageIndex;
			}			
			return;			
		}
		supl_pt_insert(&(p->supl_pt), spte);
		//pagedir_set_present(th->pagedir, addr, false);
		addr += PGSIZE;
		pageIndex++;
	}

	mapped_file *mf = (mapped_file *)malloc(sizeof(mapped_file));
	mf->id = mfd_create();
	mf->user_provided_location = addrStart;
	mf->fd = fl;
	mf->fd2 = fd;
	mf->file_size = filesize;

	struct fd_list_link* link = fd_get_link(fd);
	link->mapped = true;


	list_push_back(&p->mmap_list, &(mf->lst));
	//get_mapped_file_from_page_pointer(p, addrStart);
	f->eax = mf->id;
}
#endif

#ifdef VM
mapped_file *get_mapped_file_from_page_pointer(process_t *p, void *pagePointer) {
	struct list* file_descriptors = &(p->mmap_list);
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors); e = list_next(e)) {
		mapped_file *mmentry = list_entry(e, mapped_file, lst);
		if(mmentry->user_provided_location <= pagePointer && 
			(pagePointer <= mmentry->user_provided_location + mmentry->file_size))
				return mmentry;
	}

	return NULL;
}
#endif

#ifdef VM
void munmap_all(void) {
	process_t *cp = process_current();
	struct list* file_descriptors = &cp->mmap_list;
	struct list_elem *e;
	for (e = list_begin(file_descriptors); e != list_end(file_descriptors);) {
		mapped_file *mmentry = list_entry(e, mapped_file, lst);
		e = mummap_wrapped(mmentry);		
	}	
}
#endif


#ifdef VM
static struct list_elem* mummap_wrapped(mapped_file *fl) {
	if(fl == NULL) {
		return NULL;
	}
	struct file* cfile = fl->fd;

	if(cfile == NULL) {
		return mf_remove_file(fl);		
	}
	char *addr = (char *)fl->user_provided_location;
	uint32_t *pd = thread_current()->pagedir;
	process_t *pr_crt = process_current();

	while(addr < (char*)fl->user_provided_location + fl->file_size) {

		void *kpage = pagedir_get_page (pd, addr);
		if(kpage)
		{
			void *frame = ft_get_frame(kpage);
			if( frame != NULL && ft_atomic_pin_frame(frame))
			{
				lock_acquire(&pr_crt->shared_res_lock);
				ft_evict_frame(frame);
				lock_release(&pr_crt->shared_res_lock);
				ft_free_frame(frame);
			}
		}
		/*if(kpage && pagedir_is_dirty(pd, addr)) {
			save_page_mm(fl->fd, addr - (char*)fl->user_provided_location, kpage);
		}*/
		supl_pt_remove_spte(pr_crt, addr);
		addr += PGSIZE;
	}
	struct fd_list_link* link = fd_get_link(fl->fd2);	

	if(!link) {
		filesys_lock();
		file_close(fl->fd);
		filesys_unlock();
	}
	else {
		link->mapped = false;		
	}
	
	lock_acquire(&pr_crt->shared_res_lock);
	struct list_elem* e = mf_remove_file(fl);
	lock_release(&pr_crt->shared_res_lock);
	return e;
}
#endif

#ifdef VM
static void syscall_munmap(struct intr_frame *f) {
	f->eax = -1;
	int mfd = (int) ((int*)f->esp)[1];	
	mapped_file* fl = mfd_get_file(mfd);
	mummap_wrapped(fl);	
}
#endif

static void syscall_handler (struct intr_frame *f) 
{
	if(!is_valid_user_address_range_read(f->esp, 4)) {
		kill_current_process();		
		return;
	}
  #ifdef VM
	thread_current()->esp = f->esp;
  #endif

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
#ifdef VM
		case SYS_MMAP:
			syscall_mmap(f);
			break;
		case SYS_MUNMAP:
			syscall_munmap(f);
			break;
#endif
		default:
			thread_exit();  
		}
}

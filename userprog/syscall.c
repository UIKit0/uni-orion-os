#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/fd.h"

#ifdef VM
	#include "vm/frame.h"
	#include "vm/page.h"
	#include "vm/mmap.h"
#endif

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

#ifdef FILESYS_SUBDIRS
static void syscall_chdir(struct intr_frame *f);
static void syscall_mkdir(struct intr_frame *f);
static void syscall_readdir(struct intr_frame *f);
static void syscall_isdir(struct intr_frame *f);
static void syscall_inumber(struct intr_frame *f);
#endif

#ifdef VM
static void syscall_mmap(struct intr_frame *f);
static void syscall_munmap(struct intr_frame *f);
#endif

#ifdef VM
static supl_pte* gPages[1024];
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
	int initial_size = 0;

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
#ifdef FILESYS_SUBDIRS
	// TODO: Parse the path to see if it's a file or a directory
#endif
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
#ifdef FILESYS_SUBDIRS
	// TODO: Link the file or directory to the file descriptor
#endif
//	link->file = file;
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
	frame** frames = (frame**)malloc(nr_of_frames * sizeof(frame*));
	preload_and_pin_pages(buffer, size, frames);
#endif

	filesys_lock();
	f->eax = file != NULL ? file_read(file, buffer, size) : 0;
	filesys_unlock();

#ifdef VM
	ft_unpin_frames(frames, nr_of_frames);
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
	frame** frames = (frame**)malloc(nr_of_frames * sizeof(frame*));
	preload_and_pin_pages(buffer, size, frames);
#endif

	filesys_lock();
	f->eax = file != NULL ? file_write(file, buffer, size) : 0;
	filesys_unlock();

#ifdef VM
	ft_unpin_frames(frames, nr_of_frames);
#endif

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

#ifdef FILESYS_SUBDIRS
	// TODO: allow the closing of subdirectories
#endif

	struct fd_list_link *link = fd_get_link(fd);
	if(link->mapped == false) {
	//	file_close(link->file);
	}	
	filesys_unlock();
	fd_close(fd);
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


	//struct thread *th = thread_current();

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
static void syscall_munmap(struct intr_frame *f) {
	f->eax = -1;
	int mfd = (int) ((int*)f->esp)[1];
	mapped_file* fl = mfd_get_file(mfd);
	mummap_wrapped(fl);
}
#endif

#ifdef FILESYS_SUBDIRS
static void syscall_chdir(struct intr_frame *f) {
	char *path = (char*) ((int*)f->esp)[1];

	// TODO:
	// 1. Parse the path to ensure it exists, and create a dir* from it
	// 2. Assign the result to the current process working directory
}

static void syscall_mkdir(struct intr_frame *f) {
	char *path = (char*) ((int*)f->esp)[1];

	// TODO:
	// 1. Split the path into prefix_path + entry_name
	// 2. Ensure prefix_path exists
	// 3. Create a directory with name entry_name in prefix_path
}

static void syscall_readdir(struct intr_frame *f) {
	int fd = (int)((int*)f->esp)[1];
	char *name = (char*) ((int*)f->esp)[2];

	dir_readdir(fd_get_dir(fd), name);
}

static void syscall_isdir(struct intr_frame *f) {
	int fd = (int)((int*)f->esp)[1];
	f->eax = fd_is_directory(fd);
}

static void syscall_inumber(struct intr_frame *f) {
	int fd = (int)((int*)f->esp)[1];
	f->eax = fd_inode_number(fd);
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
#ifdef FILESYS_SUBDIRS
    	case SYS_CHDIR:
    		syscall_chdir(f);
    		break;
    	case SYS_MKDIR:
    		syscall_mkdir(f);
    		break;
    	case SYS_READDIR:
    		syscall_readdir(f);
    		break;
    	case SYS_ISDIR:
    		syscall_isdir(f);
    		break;
		case SYS_INUMBER:
			syscall_inumber(f);
			break;
#endif
		default:
			thread_exit();  
		}
}

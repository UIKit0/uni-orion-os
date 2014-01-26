#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <hash.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


static struct lock file_sys_lock;
static struct lock hash_lock;
static struct lock process_wait_lock;
static struct lock pid_lock;
static struct hash process_table;

unsigned process_hash_func (const struct hash_elem *e, void *aux);
bool process_hash_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);

void delete_process(process_t *process);
void insert_process(process_t *process);
pid_t allocate_pid (void);
void init_process( process_t* proc );
void init_master_process( process_t* proc );
void free_fd_list(void);
static bool
load_page(struct file *file, off_t ofs, uint8_t *upage,
    uint32_t read_bytes, uint32_t zero_bytes, bool writable,
    int swap_slot_no UNUSED);

unsigned process_hash_func (const struct hash_elem *e, void *aux UNUSED) {
  process_t *p = hash_entry(e, process_t, h_elem);
  return hash_int(p->pid);
}

bool process_hash_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  process_t *lhs = hash_entry(a, process_t, h_elem);
  process_t *rhs = hash_entry(b, process_t, h_elem);
  return lhs->pid < rhs->pid;
}

process_t * find_process(pid_t pid) {
  process_t dummy;
  dummy.pid = pid;
  lock_acquire(&hash_lock);
  struct hash_elem *result = hash_find(&process_table, &(dummy.h_elem));
  lock_release(&hash_lock);
  return result != NULL ? hash_entry(result, process_t, h_elem) : NULL;
}

void delete_process(process_t *proc) {
  lock_acquire(&hash_lock);  
  hash_delete(&process_table, &(proc->h_elem));
  free(proc);
  lock_release(&hash_lock);
}

void insert_process(process_t *proc) {
  lock_acquire(&hash_lock);
  hash_insert(&process_table, &(proc->h_elem));  
  lock_release(&hash_lock);
}

process_t *process_current(void) {
  #ifdef USERPROG
    process_t *result = find_process(thread_current()->pid);
    ASSERT(result);
    return result;
  #else
    return NULL;
  #endif
}

/* Initializes different mechanisms used with process kernel functions. It is called once from init.c when the OS starts */
void process_init(void) {
  lock_init(&process_wait_lock);
  lock_init(&pid_lock);
  lock_init(&hash_lock);  
  lock_init(&file_sys_lock);
  hash_init(&process_table, &process_hash_func,
    process_hash_less_func, NULL);


  process_t *p; //initial process. Father of all.
  p = (process_t *)malloc (sizeof(process_t));
  ASSERT(p);

  init_master_process(p);    
  insert_process(p);
  thread_current()->pid = p->pid;
}

pid_t allocate_pid() {
  static pid_t next_pid = 1;
  pid_t pid;
  lock_acquire(&pid_lock);
  pid = next_pid++;
  lock_release(&pid_lock);
  return pid; 
}

void init_master_process( process_t* proc) {
  proc->pid = allocate_pid();
  proc->ppid = PID_ERROR;
  proc->status = ALIVE;
  proc->exit_code = -1;
  proc->exe_file = NULL; 
  list_init( &proc->owned_file_descriptors);
  list_init( &proc->mmap_list);
  proc->num_of_opened_files = 0;
  //we don't really need the process_lock for the master process  
}

void init_process( process_t* proc ) {
  proc->pid = allocate_pid();
  proc->ppid = process_current()->pid;
  proc->status = ALIVE;
  proc->exit_code = -1;
  proc->exe_file = NULL;
  list_init( &proc->owned_file_descriptors);
  list_init( &proc->mmap_list);
  proc->num_of_opened_files = 0;
#ifdef VM
  supl_pt_init(&proc->supl_pt);
#endif
  sema_init( &(proc->process_semaphore), 0);
}

void free_fd_list(void) {
  struct list* file_descriptors = &process_current()->owned_file_descriptors;
  struct list_elem *e;
    for (e = list_begin(file_descriptors); e != list_end(file_descriptors);){
      struct fd_list_link *link = list_entry(e, struct fd_list_link, l_elem);
      e = list_next(e);
      free(link);
    }    
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *buf) 
{
  char *fn_copy, *fn_copy_name;
  process_t *p;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  
  if (fn_copy == NULL)
    return PID_ERROR;


  fn_copy_name = palloc_get_page (0);

  if(fn_copy_name == NULL) {
    palloc_free_page (fn_copy); //ensure that we are not leaking pages
    return PID_ERROR;
  }

  p = (process_t *)malloc (sizeof(process_t));

  if(p == NULL) {
    palloc_free_page (fn_copy);  //ensure that we are not leaking pages
    palloc_free_page (fn_copy_name);
    return PID_ERROR;
  }  

  strlcpy(fn_copy, buf, PGSIZE);
  strlcpy(fn_copy_name, buf, PGSIZE);

  /* Initialize process and add it into the hash table. */
  init_process(p);
  insert_process(p);

  char* save_ptr;
  char* file_name = strtok_r(fn_copy_name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid_t tid = thread_process_create(file_name, PRI_DEFAULT, start_process, fn_copy, p);

  palloc_free_page(fn_copy_name);

  if (tid == TID_ERROR) {
    palloc_free_page(fn_copy);
    delete_process(p);    
    return PID_ERROR;
  }

  sema_down(&(p->process_semaphore));
  
  if( p->status == INVALID ) {
    delete_process(p);    
    return PID_ERROR;
  }
  return p->pid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  char* save_ptr;
  char* file_name_exe = strtok_r( file_name, " ", &save_ptr );
  
  lock_acquire(&file_sys_lock);
  success = load (file_name_exe, &if_.eip, &if_.esp);
  lock_release(&file_sys_lock);

  if ( success )
  {
    char* token;
    int sum_of_length = 0;

    int argumentsAddress[ 100 ];
    int argumentsCount = 0;

    
    while( !*(--save_ptr) ) *save_ptr = ' ';
    save_ptr = file_name;

    for( token = strtok_r( NULL, " ", &save_ptr ); token != NULL;
	       token = strtok_r( NULL, " ", &save_ptr ) )
    {
      if_.esp -= 1;
      memcpy( if_.esp, "\0", 1 );
      
      if_.esp -= strlen( token );
      memcpy( if_.esp, token, strlen( token ) );

      argumentsAddress[ argumentsCount ] = (int)if_.esp;//
      argumentsCount++;

      sum_of_length += strlen( token ) + 1;
    }

    sum_of_length %= 4;
    if_.esp -= sum_of_length;
    memcpy( if_.esp, "\0", sum_of_length );
       

    if_.esp -= 4;
    memcpy( if_.esp, "\0\0\0\0", 4 );

    int i = 0;

    for ( i = argumentsCount - 1; i > -1; --i )
    {
      if_.esp -= 4;
      memcpy( if_.esp, &argumentsAddress[ i ], 4 );
    }
    
    int argvAddress = (int)if_.esp;
    if_.esp -= 4;
    memcpy( if_.esp, &argvAddress, 4 );


    if_.esp -= 4;
    memcpy( if_.esp, &argumentsCount, 4 );

    if_.esp -= 4;
    memcpy( if_.esp, "\0\0\0\0", 4 );   
    
  }
  
  /* If load failed, quit. */
  palloc_free_page (file_name);

  process_t *p = process_current();
  
  if (!success) {    
    p->status = INVALID;    
    thread_exit ();
  } else {    
    sema_up (&(p->process_semaphore));
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (pid_t child_tid) 
{
  process_t *child = find_process(child_tid);
  process_t *current = process_current();

  if(child == NULL) {
    return -1;
  }

  if(current->pid != child->ppid) {
    return -1;
  }

  int exit_code = -1;

  if(child->status == KILLED) {
    exit_code = -1;
  }
  else if(child->status == DEAD) {
    exit_code = child->exit_code;
  }
  else if(child->status == ALIVE) {    
    sema_down (&(child->process_semaphore));
    exit_code = child->exit_code;
    delete_process(child);    
  }  
  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread * cur = thread_current();
  process_t *current = process_current();
  uint32_t *pd;

  int exit_code;

  if(current->status == INVALID) {
    exit_code = PID_ERROR;
  }
  else {
    exit_code = current->exit_code;
  }
  munmap_all();
  lock_acquire(&file_sys_lock);  
  free_fd_list();

#ifdef VM
  //supl_pt_free(&(current->supl_pt));
#endif

  if(current->exe_file != NULL) {    
    file_allow_write(current->exe_file);    
    file_close(current->exe_file);    
  }
  lock_release(&file_sys_lock);
  //other cleanup here please
  
  printf("%s: exit(%d)\n", cur->name, exit_code);
  sema_up (&(current->process_semaphore));
  
  
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
//static bool load_page_lazy(uint8_t *upage);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
    
  if (file == NULL) 
  {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
  }
  
   

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  if(success) {
    process_current()->exe_file = file;
    file_deny_write(file);
  }
  else {
    file_close(file);
  }
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Calculate how to fill this page.
		 We will read PAGE_READ_BYTES bytes from FILE
		 and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
		/* Save page info in supplemental page table*/
		supl_pte *spte = (supl_pte*)malloc(sizeof(supl_pte));
		spte->virt_page_no = pg_no(upage);
		spte->ofs = ofs;
		spte->page_read_bytes = page_read_bytes;
		spte->page_zero_bytes = page_zero_bytes;
		spte->virt_page_addr = upage;
		spte->writable = writable;
		spte->swap_slot_no = -1;

		supl_pt_insert(&(process_current()->supl_pt), spte);

#else
		if(!load_page(file, ofs, upage, read_bytes, zero_bytes, writable, -1))
			return false;
#endif
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

bool load_page_mm(struct file* fd, int ofs, uint8_t *upage) {
  frame* frame = ft_alloc_frame(true, upage);  
  if(frame == NULL) {
    PANIC("Kernel panic - Insufficient memory");
  }

  struct thread *crt_thread = thread_current();

  pagedir_set_page(crt_thread->pagedir, upage, frame->kpage, true);
  pagedir_set_present(crt_thread->pagedir, upage, true);
  pagedir_set_dirty(crt_thread->pagedir, upage, false);

  filesys_lock();
  int beforeoff = file_tell(fd);
  file_seek(fd, ofs);
  int filesize = file_length(fd);
  int bytesToRead = filesize < PGSIZE + ofs ? filesize - ofs : PGSIZE;
  file_read(fd, frame->kpage, bytesToRead);
  file_seek(fd, beforeoff);
  filesys_unlock(); 

  //printf("bytesToRead: %d\nupage: %p\nkpage: %p\n", bytesToRead, upage, frame->kpage);

  return true;  
}

bool save_page_mm(struct file* fd, int ofs, uint8_t *kpage) {
  filesys_lock();
  int beforeoff = file_tell(fd);
  
  file_seek(fd, ofs);
  int filesize = file_length(fd);
  int bytesToWrite = filesize < PGSIZE + ofs ? filesize - ofs : PGSIZE;
  file_write(fd, kpage, bytesToWrite);
  file_seek(fd, beforeoff);
  filesys_unlock();
  return true;
}

static bool
load_page(struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable,
		int swap_slot_no UNUSED)
{
	uint8_t *kpage;

#ifdef VM
	frame* frame = ft_alloc_frame(false, upage);
	if(frame == NULL)
	{
		PANIC("Kernel panic - Insufficient memory");
	}

	kpage = frame->kpage;
#else
	kpage = palloc_get_page(PAL_USER);
	if (kpage == NULL )
		return false;
#endif

#ifdef VM
	if(swap_slot_no >= 0)
	{
		//printf("[SWAP] swaping in user page %p \n", upage);
		frame->pinned = true;
		swap_in(kpage, swap_slot_no);
		frame->pinned = false;
		pagedir_set_page(thread_current()->pagedir, upage, kpage, writable);
		pagedir_set_present(thread_current()->pagedir, upage, true);
		//if loaded from swap, surely it's not in the file => it's dirty
		pagedir_set_dirty(thread_current()->pagedir, upage, true);
		return true;
	}
#endif

	//advance to offset - might be redundant when VM is not implemented, but is safer
	if(file != NULL)
	{
		file_seek(file, ofs);

		/* Load this page. */
#ifdef VM
		frame->pinned = true;
#endif
		if (file_read(file, kpage, read_bytes) != (int) read_bytes)
		{
	#ifdef VM
			ft_free_frame(frame);
	#else
			palloc_free_page(kpage);
	#endif
			return false;
		}
	}

	memset(kpage + read_bytes, 0, zero_bytes);
#ifdef VM
		frame->pinned = false;
#endif

#ifdef VM
	/* Set the page present if it is in the process's adress space*/
	struct thread *crt_thread = thread_current();
	if(pagedir_get_page(crt_thread->pagedir, upage) != NULL)
	{
		pagedir_set_present(crt_thread->pagedir, upage, true);
		return true;
	}
#endif
	/* Add the page to the process's address space. */
	if (!install_page(upage, kpage, writable))
	{
#ifdef VM
		ft_free_frame(frame);
#else
		palloc_free_page(kpage);
#endif
		return false;
	}

	return true;
}

bool
load_page_lazy(process_t *p, supl_pte *spte)
{
	struct file *file = p->exe_file;
	size_t page_read_bytes = spte->page_read_bytes;
	size_t page_zero_bytes = spte->page_zero_bytes;
	bool writable = spte->writable;
	off_t ofs = spte->ofs;
	void *upage = spte->virt_page_addr;
	int swap_slot_no = spte->swap_slot_no;
	spte->swap_slot_no = -1;

	return load_page(file, ofs, upage, page_read_bytes, page_zero_bytes, writable, swap_slot_no);
}

bool
stack_growth(int nr_of_pages)
{
  #ifdef VM
	//check if stack limit exceeded
	if ((thread_current()->numberOfStackGrows + nr_of_pages) * PGSIZE >= MAX_STACK_SIZE)
	{
		return false;
	}
  #endif

	void *upage;

	while (nr_of_pages)
	{
	  #ifdef VM
		upage = thread_current()->last_stack_page - PGSIZE;
	  #else
		upage = PHYS_BASE - PGSIZE;
      #endif

		if(load_page(NULL, 0, upage, 0, PGSIZE, true, -1))
		{
		  #ifdef VM
			thread_current()->last_stack_page = upage;
			thread_current()->numberOfStackGrows++;

			//Saves the page info into supplemental table
			supl_pte *spte = (supl_pte*)malloc(sizeof(supl_pte));
			spte->virt_page_no = pg_no(upage);
			spte->virt_page_addr = upage;
			spte->swap_slot_no = -1;
			spte->writable = true;
			spte->page_read_bytes = 0; //this page will be always written to swap

			supl_pt_insert(&(process_current()->supl_pt), spte);
		  #endif
		}
		else
		{
			return false;
		}
		--nr_of_pages;
	}

	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  if(stack_growth(1))
  {
	  *esp = PHYS_BASE;
	  return true;
  }
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


void filesys_lock(void) {
  lock_acquire(&file_sys_lock);  
}

void filesys_unlock(void) {
  lock_release(&file_sys_lock);
}

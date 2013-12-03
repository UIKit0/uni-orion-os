Filesystem System calls
=======================

Initial Functionality
---------------------
At the beginning of this project, pintOS does not provide the posibility of running any kind of user programs, much less make system calls from them. Moreover, there is no data structure to support processes. Our purpose is to implement the data structures required to support the creation and running of processes, as well as implementing the various system calls related to processes (creation, execution, termination) and file input/output (creating, reading, writing, etc).

Data Structures and Functions
------------------------------
Most filesystem system calls require the use of a file descriptor to identify the file they work with. We will be using an integer value to represent such a descriptor in user programs, but the kernel will require to map the integer file descriptor to a file structure. Since file descriptors are owned by processes, the process structure must contain the list of file descriptors it owns.

/* Structure used to identify an open file through an integer descriptor. */
struct file_descriptor {
	/* Identifier used in user mode. */
	int id;
	/* Reference to the file structure identified through this descriptor. */
	struct file* file_ref;
	/* List element used for the process list. */
	struct list_elem elem;
}

/* The process structure must keep track of the assigned file descriptors. */
struct process {
	struct list owned_file_descriptors;
}

As such, when a system call is made, the kernel can look up the file_descriptor in the process's list, based on the identifier recieved when making the call. If a file_descriptor with that id is not found, the OS ignores the call (but it can choose to act otherwise if needed).
When a process is closed, whether naturally or forced, the kernel must go through all the file descriptors and make sure to close each one.

To handle the system calls, we will implement the function "static void syscall_handler(struct intr_frame *f)"" from syscall.c. The function will need to unwind the stack using the intr_frame, and retrieve the syscall id, as well as retrieving all the necessary arguments. It will then delegate the execution to a specialized function, sending the arguments it found on the stack. Also, the functions are responsible for leaving the return value in the proper location. Here are the functions that will be implemented for the filesystem system calls:

/* Handles the SYS_CREATE system call. */
void create(const char *file, unsigned initial_size)

/* Handles the SYS_REMOVE system call. */
void remove(const char* file)

/* Handles the SYS_OPEN system call. */
void open(const char* file)

/* Handles the SYS_FILESIZE system call. */
void filesize(int fd)

/* Handles the SYS_READ system call. */
void read(int fd, void *buffer, unsigned size)

/* Handles the SYS_WRITE system call. */
void write(int fd, const void *buffer, unsigned size)

/* Handles the SYS_SEEK system call. */
void seek(int fd, unsigned position)

/* Handles the SYS_TELL system call. */
void tell(int fd)

/* Handles the SYS_CLOSE system call. */
void close(int fd)

Functionality
-------------
Each handling function will need to first and foremost validate the addresses provided by the user program. It will then invoke the necessary functions provided by the filesystem and found in filesys/file.h. The filesystem functions work with file structures, but our handling functions are provided with file descriptors. As such, each handling function must find the file structure corresponding to the file descriptor, by iterating through the list of file_descriptors owned by the process and matching the file_descriptor id with the integer identifier provided by the user program.

Design Decisions
----------------
Every system call that is required to write or read data to or from user space must ensure the address or range is valid and owned by the process that made the system call. For this purpose we found two ways of implementation:

The first one ...
The second one ...

We chose the second one because ...

Tests
-----
Except for the user programs (which are quite numerous), I was unable to find any tests or testing mechanism. As such, I am unable to analyze how the tests will be handled by the OS.

Observations
------------
Where are the tests?
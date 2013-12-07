#ifndef USERPROG_COMMON_H
#define USERPROG_COMMON_H
/* forward declarations */

/* Process identifier type.
   You can redefine this to whatever type you like. */
typedef int pid_t;

static pid_t const PID_ERROR = -1;

struct process_t;
typedef struct process_t process_t;


enum pstatus_t {
	ALIVE, 
	KILLED, 
	DEAD
};

#endif

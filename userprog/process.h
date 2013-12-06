#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "userprog/common.h"
#include "threads/thread.h"

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);
void process_init(void);

process_t *process_current(void);
process_t * find_process(pid_t pid);

#endif /* userprog/process.h */

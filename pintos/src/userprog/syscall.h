#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

//lock for file system
struct lock file_lock;

void syscall_init (void);
void close_all(void);

#endif /* userprog/syscall.h */

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include <user/syscall.h>

static void syscall_handler (struct intr_frame *);


static void check_user_addr(const void *vaddr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  
  int *ptr = f->esp;
  check_user_addr(ptr);
  int syscall = *ptr;
  printf ("system call!\n");

  switch(syscall){
  	case SYS_HALT:
  		printf("SYS_HALT\n");
      halt();
  		break;

  	case SYS_EXIT:
  		printf("SYS_EXIT\n");
  		break;

  	case SYS_EXEC:
  		printf("SYS_EXEC\n");
  		break;

  	case SYS_WAIT:
  		printf("SYS_WAIT\n");
  		break;

  	case SYS_CREATE:
  		printf("SYS_CREATE\n");
  		break;

  	case SYS_REMOVE:
  		printf("SYS_REMOVE\n");
  		break;

  	case SYS_OPEN:
  		printf("SYS_OPEN\n");
  		break;

  	case SYS_FILESIZE:
  		printf("SYS_FILESIZE\n");
  		break;

  	case SYS_READ:
  		printf("SYS_READ\n");
  		break;

  	case SYS_WRITE:
  		printf("SYS_WRITE\n");
  		break;

  	case SYS_SEEK:
  		printf("SYS_SEEK\n");
  		break;

  	case SYS_TELL:
  		printf("SYS_TELL\n");
  		break;

  	case SYS_CLOSE:
  		printf("SYS_CLOSE\n");
  		break;
  }
  thread_exit ();
}




void halt (void){
	shutdown_power_off();
}

/*
void exit (int status){

}



pid_t exec (const char *file){

}

int wait (pid_t){

}

bool create (const char *file, unsigned initial_size){

}

bool remove (const char *file){

}
int open (const char *file){

}
int filesize (int fd){

}
int read (int fd, void *buffer, unsigned length){

}
int write (int fd, const void *buffer, unsigned length){

}
void seek (int fd, unsigned position){

}
unsigned tell (int fd){

}
void close (int fd){

}
*/


static void check_user_addr(const void *vaddr){
	if(is_kernel_vaddr(vaddr)){
		printf("not valid addr : %p\n", vaddr);
	}
}


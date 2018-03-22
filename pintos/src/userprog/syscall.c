#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include <user/syscall.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <string.h>
#include "threads/malloc.h"

static void syscall_handler (struct intr_frame *);


static void check_addr(const void *vaddr);
static void get_args(int *esp, int *args, int count);

static struct lock file_lock;


//to save in file_list in thread
struct file_list_elem{
  int fd;
  struct file *f;
  struct list_elem elem;
};

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  
  int *esp= f->esp;
  check_addr(esp);
  int syscall = *esp;
  int args[3];
  //printf ("system call!\n");
  //printf("current esp : %p\n", esp);
  //printf("current esp + 1 : %p\n", esp+1);

  switch(syscall){
  	case SYS_HALT:
  		printf("SYS_HALT\n");
      halt();
  		break;

  	case SYS_EXIT:
  		printf("SYS_EXIT\n");
      get_args(esp, args, 1);
      exit(args[0]);
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
      get_args(esp, args, 1);
      lock_acquire(&file_lock);
      f->eax = open((const char *)args[0]);
  		break;

  	case SYS_FILESIZE:
  		printf("SYS_FILESIZE\n");
  		break;

  	case SYS_READ:
  		printf("SYS_READ\n");
  		break;

  	case SYS_WRITE:
  		//printf("SYS_WRITE\n");
      get_args(esp, args, 3);
      lock_acquire(&file_lock);
      f->eax = write(args[0], (void *) args[1], (unsigned) args[2]);
      lock_release(&file_lock);
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
  //thread_exit ();
}




void halt (void){
	shutdown_power_off();
}


void exit (int status){
  //need to do file close

  thread_current()->exit_status = status;
  thread_exit();
}

/*

pid_t exec (const char *file){

}

int wait (pid_t){

}

bool create (const char *file, unsigned initial_size){

}

bool remove (const char *file){

}
*/


int open (const char *file){
  check_addr((const void *) file);
  struct file *f = filesys_open(file);
  
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  else{
    struct file_list_elem *flelem = malloc(sizeof(struct file_list_elem));
    flelem->f = f;
    flelem->fd = thread_current()->fd_count;
    thread_current()->fd_count++;
    list_push_back(&thread_current()->fd_list, &flelem->elem);
    lock_release(&file_lock);
    return flelem->fd;
  }
}


/*
int filesize (int fd){


}

int read (int fd, void *buffer, unsigned length){
  check_addr(buffer);
  if(fd == 0){

  }
}
*/



int write (int fd, const void *buffer, unsigned length){
  check_addr(buffer);
  if(fd == 1){
    putbuf(buffer, length);
  }
  else{

  }
  return length;
}



/*
void seek (int fd, unsigned position){

}
unsigned tell (int fd){

}
void close (int fd){

}
*/

static void get_args(int *esp, int *args, int count){
  int i;
  int *temp_ptr;
  for(i=0; i<count; i++){
    temp_ptr = esp + 1 + i;
    check_addr((const void *) temp_ptr);
    args[i] = *temp_ptr;
  }
  /*
  for(i=0; i<count; i++){
    printf("%d: %p\n", i, args[i]);
  }
  */
}

static void check_addr(const void *vaddr){
	if(is_kernel_vaddr(vaddr)){
		printf("not valid addr : %p\n", vaddr);
	}
}


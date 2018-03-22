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
#include "userprog/process.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);


static void get_args(int *esp, int *args);
static struct file *get_file_by_fd(int fd);

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
  int syscall = *esp;
  int args[3];
  get_args(esp, args);
  //printf ("system call!\n");
  //printf("current esp : %p\n", esp);
  //printf("current esp + 1 : %p\n", esp+1);

  switch(syscall){
  	case SYS_HALT:
  		//printf("\nSYS_HALT\n");
      halt();
  		break;
  	case SYS_EXIT:
  		//printf("\nSYS_EXIT\n");
      lock_acquire(&file_lock);
      exit(args[0]);
  		break;

  	case SYS_EXEC:
  		//printf("\nSYS_EXEC\n");
      f->eax = exec((const char*) args[0]);
  		break;

  	case SYS_WAIT:
  		//printf("\nSYS_WAIT\n");
  		break;

  	case SYS_CREATE:
  		//printf("\nSYS_CREATE\n");
      lock_acquire(&file_lock);
      f->eax = create((const char *)args[0], args[1]);
  		break;

  	case SYS_REMOVE:
  		//printf("\nSYS_REMOVE\n");
      lock_acquire(&file_lock);
      f->eax = remove((const char *)args[0]);
  		break;

  	case SYS_OPEN:
  		//printf("\nSYS_OPEN\n");
      lock_acquire(&file_lock);
      f->eax = open((const char *)args[0]);
  		break;

  	case SYS_FILESIZE:
  		//printf("\nSYS_FILESIZE\n");
      lock_acquire(&file_lock);
      f->eax = filesize(args[0]);
  		break;

  	case SYS_READ:
  		//printf("\nSYS_READ\n");
      f->eax = read(args[0], (void *) args[1], (unsigned) args[2]);
  		break;

  	case SYS_WRITE:
  		//printf("\nSYS_WRITE\n");
      f->eax = write(args[0], (void *) args[1], (unsigned) args[2]);
  		break;

  	case SYS_SEEK:
  		//printf("\nSYS_SEEK\n");
      lock_acquire(&file_lock);
      seek(args[0], (unsigned) args[1]);
  		break;

  	case SYS_TELL:
  		//printf("\nSYS_TELL\n");
      lock_acquire(&file_lock);
      tell(args[0]);
  		break;

  	case SYS_CLOSE:
  		//printf("\nSYS_CLOSE\n");
      lock_acquire(&file_lock);
      close(args[0]);
  		break;
  }

}




void halt (void){
	shutdown_power_off();
}


void exit (int status){
  struct thread *t = thread_current();
  //close all file opened in current thread
  struct list_elem *e;
  for(e=list_begin(&t->file_list); e!=list_end(&t->file_list); e=list_next(e)){
    struct file_list_elem *fle = list_entry(e, struct file_list_elem, elem);
    file_close(fle->f);
    list_remove(&fle->elem);
    free(fle);
  }

  t->exit_status = status;
  lock_release(&file_lock);
  printf("%s: exit(%d)\n", thread_current()->name, status);
  struct thread *parent = t->parent;
  sema_up(&parent->sema);
  thread_exit();
}


pid_t exec (const char *file){
  tid_t tid = process_execute(file);
  struct thread *t = thread_current();
  if(t->exit_status == -1)
    return -1;
  else
    return (pid_t) tid;
}
/*

int wait (pid_t){

}
*/

bool create (const char *file, unsigned initial_size){
  //create file
  bool b = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return b;
}

bool remove (const char *file){
  //remove file
  bool b = filesys_remove(file);
  lock_release(&file_lock);
  return b;
}


int open (const char *file){
  //open file
  struct file *f = filesys_open(file);
  //if file is null return error
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  //if file is not null
  else{
    //make file list elem and put it to file list of the thread and return fd
    struct file_list_elem *fle = malloc(sizeof(struct file_list_elem));
    fle->f = f;
    fle->fd = thread_current()->fd_count;
    thread_current()->fd_count++;
    list_push_back(&thread_current()->file_list, &fle->elem);
    lock_release(&file_lock);
    return fle->fd;
  }
}

int filesize (int fd){
  struct file *f = get_file_by_fd(fd);
  //if there is no file with fd
  if(f==NULL){
    lock_release(&file_lock);
    return -1;
  }
  //if there is file with fd
  else{
    lock_release(&file_lock);
    return file_length(f);
  }
}


int read (int fd, void *buffer, unsigned length){
  unsigned int i;
  //STDIN CASE
  if(fd == 0){
    //put input_getc result in buffer
    for(i=0; i<length; i++){
      ((char *)buffer)[i] = input_getc();
    }
    return length;
  }
  else{
    lock_acquire(&file_lock);
    struct file *f = get_file_by_fd(fd);
    //if no file in file_list
    if(f == NULL){
      lock_release(&file_lock);
      return -1;
    }
    //read file f
    else{
      off_t result = file_read(f, buffer, length);
      lock_release(&file_lock);
      return result;
    }
  }
}


int write (int fd, const void *buffer, unsigned length){
  //STDOUT CASE
  if(fd == 1){
    putbuf(buffer, length);
    return length;
  }

  else{
    lock_acquire(&file_lock);
    struct file *f = get_file_by_fd(fd);
    //if no file in file_list
    if(f == NULL){
      lock_release(&file_lock);
      return -1;
    }
    //write at file f
    else{
      off_t result = file_write(f, buffer, length);
      lock_release(&file_lock);
      return result;
    }
  }
}

void seek (int fd, unsigned position){
  struct file *f = get_file_by_fd(fd);
  //if no fd in file_list
  if(f == NULL)
    lock_release(&file_lock);
  else{
    file_seek(f, position);
    lock_release(&file_lock);
  }
}


unsigned tell (int fd){
  struct file *f = get_file_by_fd(fd);
  //if no fd in file system
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  else{
    off_t temp = file_tell(f);
    lock_release(&file_lock);
    return temp;
  }
}

void close (int fd){
  struct thread *t = thread_current();
  struct list_elem *e;
  for(e=list_begin(&t->file_list); e!=list_end(&t->file_list); e=list_next(e)){
    struct file_list_elem *fle = list_entry(e, struct file_list_elem, elem);
    if(fle->fd == fd){
      file_close(fle->f);
      list_remove(&fle->elem);
      free(fle);
      break;
    }
  }
  lock_release(&file_lock);
}

//to get arguments of current esp
static void get_args(int *esp, int *args){
  int i;
  int *temp_ptr;
  for(i=0; i<3; i++){
    temp_ptr = esp + 1 + i;
    args[i] = *temp_ptr;
  }
}

//get current thread's file list and find file by fd
static struct file *get_file_by_fd(int fd){
  struct thread *t = thread_current();
  struct list_elem *e;
  for(e=list_begin(&t->file_list); e!=list_end(&t->file_list); e=list_next(e)){
    struct file_list_elem *fle = list_entry(e, struct file_list_elem, elem);
    if(fle->fd == fd){
      return fle->f;
    }
  }
  return NULL;
}


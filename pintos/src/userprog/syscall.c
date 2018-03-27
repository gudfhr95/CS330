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
#include "threads/palloc.h"

static void syscall_handler (struct intr_frame *);


#define PRINT 0    //for debugging

void check_addr(void* vaddr);
static uintptr_t* get_arg(void* esp, int num);
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
  
  uintptr_t* esp= f->esp;
  int syscall = *esp;

  switch(syscall){
  	case SYS_HALT:
      if(PRINT)
  		  printf("\nSYS_HALT\n");
      halt();
  		break;
  	case SYS_EXIT:
      if(PRINT)
  		  printf("\nSYS_EXIT\n");
      exit((int) *get_arg(esp, 0));
  		break;

  	case SYS_EXEC:
      if(PRINT)
  		  printf("\nSYS_EXEC\n");
      f->eax = exec((const char*) *get_arg(esp, 0));
  		break;

  	case SYS_WAIT:
      if(PRINT)
  		  printf("\nSYS_WAIT\n");
      f->eax = wait((pid_t) *get_arg(esp,0));
  		break;

  	case SYS_CREATE:
      if(PRINT)
  		  printf("\nSYS_CREATE\n");
      f->eax = create((const char *) *get_arg(esp, 0), (unsigned) *get_arg(esp, 1));
  		break;

  	case SYS_REMOVE:
      if(PRINT)
  		  printf("\nSYS_REMOVE\n");
      f->eax = remove((const char *) *get_arg(esp, 0));
  		break;

  	case SYS_OPEN:
      if(PRINT)
  		  printf("\nSYS_OPEN\n");
      f->eax = open((const char *) *get_arg(esp, 0));
  		break;

  	case SYS_FILESIZE:
      if(PRINT)
  		  printf("\nSYS_FILESIZE\n");
      lock_acquire(&file_lock);
      f->eax = filesize((int) *get_arg(esp, 0));
  		break;

  	case SYS_READ:
      if(PRINT)
  		  printf("\nSYS_READ\n");
      f->eax = read((int) *get_arg(esp, 0), (void *) *get_arg(esp, 1), (unsigned) *get_arg(esp, 2));
  		break;

  	case SYS_WRITE:
      if(PRINT)
  		    printf("\nSYS_WRITE\n");
      f->eax = write((int) *get_arg(esp, 0), (void *) *get_arg(esp, 1), (unsigned) *get_arg(esp, 2));
  		break;

  	case SYS_SEEK:
      if(PRINT)
  		  printf("\nSYS_SEEK\n");
      seek((int) *get_arg(esp, 0), (unsigned) *get_arg(esp, 1));
  		break;

  	case SYS_TELL:
      if(PRINT)
  		  printf("\nSYS_TELL\n");
      tell((int) *get_arg(esp, 0));
  		break;

  	case SYS_CLOSE:
      if(PRINT)
  		    printf("\nSYS_CLOSE\n");
      close((int) *get_arg(esp, 0));
  		break;
  }

}




void halt (void){
	shutdown_power_off();
}


void exit (int status){
  
  thread_current()->exit_status = status;

  printf("%s: exit(%d)\n", thread_current()->name, status);

  thread_exit();
}


pid_t exec (const char *file){
  tid_t tid = process_execute(file);
  return (pid_t) tid;
}


int wait (pid_t pid){
  pid_t temp_pid = (pid_t) process_wait((tid_t) pid);
  return temp_pid;
}


bool create (const char *file, unsigned initial_size){
  //if file name is NULL
  if(file == NULL){
    exit(-1);
  }
  //create file
  lock_acquire(&file_lock);
  bool b = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return b;
}

bool remove (const char *file){
  //remove file
  lock_acquire(&file_lock);
  bool b = filesys_remove(file);
  lock_release(&file_lock);
  return b;
}


int open (const char *file){
  lock_acquire(&file_lock);
  //if file is NULL
  if(file == NULL){
    lock_release(&file_lock);
    exit(-1);
  }
  //if file is not null
  else{
    //open file
    struct file *f = filesys_open(file);
    //if f is NULL
    if(f == NULL){
      lock_release(&file_lock);
      return -1;
    }
    //make file list elem and put it to file list of the thread and return fd
    struct file_list_elem *fle = calloc (1, sizeof (struct file_list_elem));
    fle->f = f;
    fle->fd = thread_current()->fd_count++;
    thread_current()->fd_count++;
    //printf("FLE : %p\n", fle);
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
    //if reading kernel vaddr
    if(is_kernel_vaddr(buffer+length)){
      lock_release(&file_lock);
      exit(-1);
    }
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
  lock_acquire(&file_lock);
  //STDOUT CASE
  if(fd == 1){
    putbuf(buffer, length);
    lock_release(&file_lock);
    return length;
  }
  else{ 
    //if reading kernel vaddr
    if(is_kernel_vaddr(buffer+length)){
      lock_release(&file_lock);
      exit(-1);
    }

    struct file *f = get_file_by_fd(fd);
    //if no file in file_list
    if(f == NULL){
      lock_release(&file_lock);
      return -1;
    }
    else{
      off_t result = file_write(f, buffer, length);
      lock_release(&file_lock);
      return result;
    }
  }
}

void seek (int fd, unsigned position){
  lock_acquire(&file_lock);
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
  lock_acquire(&file_lock);
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
  if(file_lock.holder != thread_current())
    lock_acquire(&file_lock);
  struct list_elem *e;
  for(e=list_begin(&t->file_list); e!=list_end(&t->file_list); e=list_next(e)){
    struct file_list_elem *fle = list_entry(e, struct file_list_elem, elem);
    if(fle->fd == fd){
      file_close(fle->f);
      list_remove(&fle->elem);
      free(fle);
      //printf("FLE : %p\n", fle);
      break;
    }
  }

  
  lock_release(&file_lock);
}


//check whether vaddr is valid addr, if not, exit
void check_addr(void* vaddr){
  if(is_kernel_vaddr(vaddr)){
    exit(-1);
  }
}

//to get arguments of current esp
static uintptr_t* get_arg(void* esp, int num){
  void* vaddr = esp + 4 + 4*num;
  check_addr(vaddr);
  return (uintptr_t *) vaddr;
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

void close_all(){
  struct thread *t = thread_current();
  //close all file opened in current thread
  //struct list_elem *e;
  while(!list_empty(&t->file_list)){
    struct file_list_elem *fle = list_entry(list_pop_front(&t->file_list), struct file_list_elem, elem);
    close(fle->fd);
  }
  //printf("FUCK CLOSE ALL\n");
}


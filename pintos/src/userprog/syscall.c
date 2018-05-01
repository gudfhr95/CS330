#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);


#define PRINT 0    //for debugging

void check_addr(void* vaddr);
static uintptr_t* get_arg(void* esp, int num);
static struct file *get_file_by_fd(int fd);


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
      f->eax = tell((int) *get_arg(esp, 0));
  		break;

  	case SYS_CLOSE:
      if(PRINT)
  		    printf("\nSYS_CLOSE\n");
      close((int) *get_arg(esp, 0));
  		break;

    case SYS_MMAP:
      if(PRINT)
        printf("\nSYS_MMAP\n");
      f->eax = mmap((int) *get_arg(esp,0), (void *) *get_arg(esp, 1));
      break;

    case SYS_MUNMAP:
      if(PRINT)
        printf("\nSYS_MUNMAP\n");
      munmap((int) *get_arg(esp, 0));
      break;
  }

}




void halt (void){
	shutdown_power_off();
}


void exit (int status){
  //save exit status
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_current()->name, status);
  //exit thread
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
  //for exit(-1) case
  if(file_lock.holder != thread_current())
    lock_acquire(&file_lock);
  //find by fd and remove file and free fle
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


mapid_t mmap (int fd, void *addr){
  // if mapping to NULL address
  if(addr == NULL){
    return -1;
  }
  // if addr is misalligned
  if(addr != pg_round_down(addr)){
    return -1;
  }

  struct file *f = get_file_by_fd(fd);
  struct file *mmap_file = file_reopen(f);

  off_t ofs = 0;
  uint32_t read_bytes = file_length(mmap_file);

  while (read_bytes > 0){
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    if(!page_table_add_entry(mmap_file, ofs, addr, page_read_bytes, page_zero_bytes, true, true)){
      return -1;
    }

    read_bytes -= page_read_bytes;
    ofs += page_read_bytes;

    void *upage = pg_round_down(addr);
    struct page_table_entry *pte = page_table_lookup_by_upage(upage);
    page_load_file(pte);

    addr += PGSIZE;
  }

  thread_current()->mmap_count++;
  return thread_current()->mmap_count - 1;
}

void munmap (mapid_t id){
  struct thread *t = thread_current();
  struct list_elem *e;
  for(e=list_begin(&t->mmap_list); e!=list_end(&t->mmap_list); e=list_next(e)){
    struct mmap_entry *me = list_entry(e, struct mmap_entry, elem);
    if(me->mmap_id == id){
      // if pagedir is dirty, write at file
      if(pagedir_is_dirty(t->pagedir, me->pte->upage)){
        lock_acquire(&file_lock);
        file_write_at(me->pte->file, me->pte->upage, me->pte->page_read_bytes, me->pte->ofs);
        lock_release(&file_lock);
      }
      //remove fte
      list_remove(&me->pte->fte->elem);
      palloc_free_page(me->pte->fte->paddr);
      free(me->pte->fte);
      //remove pte
      hash_delete(&t->spt, &me->pte->hash_elem);
      pagedir_clear_page(t->pagedir, me->pte->upage);
      free(me->pte);
      //remove me
      list_remove(&me->elem);
      //free(me);
    }
  }
}




//check whether vaddr is valid addr, if not, exit
void check_addr(void* vaddr){
  if(is_kernel_vaddr(vaddr)){
    exit(-1);
  }
}

//get arguments of current esp
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

//close all files in current thread
void close_all(void){
  struct thread *t = thread_current();
  while(!list_empty(&t->file_list)){
    struct file_list_elem *fle = list_entry(list_pop_front(&t->file_list), struct file_list_elem, elem);
    file_close(fle->f);
    list_remove(&fle->elem);
    free(fle);
  }
}

/* unmap all */
void unmap_all(void){
  unsigned i;
  for(i=2; i<((unsigned)thread_current()->mmap_count); i++){
    munmap(i);
  }
}

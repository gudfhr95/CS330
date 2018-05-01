#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"



/* init frame table */
void frame_table_init(void){
  list_init(&frame_table);
  lock_init(&frame_lock);
}


/* get one page from frame table */
void *frame_get_page(enum palloc_flags flags){
  lock_acquire(&frame_lock);
  void *paddr = palloc_get_page(flags);
  //if there is space
  if(paddr){
    lock_release(&frame_lock);
    return paddr;
  }
  //if there is no space in kernel space
  else{
    //swap out victim
    struct frame_table_entry *victim = frame_find_victim();
    victim->pte->sector_index = swap_out(victim->paddr);
    victim->pte->is_swapped = true;
    pagedir_clear_page(victim->thread->pagedir, victim->pte->upage);
    list_remove(&victim->elem);
    palloc_free_page(victim->paddr);
    free(victim);
    paddr = palloc_get_page(flags);
    lock_release(&frame_lock);
    return paddr;
  }
}


/* find victim for frame table */
struct frame_table_entry *frame_find_victim(void){
  struct list_elem *e = list_begin(&frame_table);
  return list_entry(e, struct frame_table_entry, elem);
}

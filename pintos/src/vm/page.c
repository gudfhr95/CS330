#include "vm/page.h"
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "filesys/file.h"

/* Returns a hash value for page p. */
unsigned page_table_hash(const struct hash_elem *p_, void *aux UNUSED){
  const struct page_table_entry *p = hash_entry(p_, struct page_table_entry, hash_elem);
  return hash_bytes(&p->upage, sizeof(p->upage));
}

/* Returns true if page a precedes page b. */
bool page_table_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
  const struct page_table_entry *a = hash_entry(a_, struct page_table_entry, hash_elem);
  const struct page_table_entry *b = hash_entry(b_, struct page_table_entry, hash_elem);
  return a->upage < b->upage;
}

/* for destroying and freeing all elements in page table */
void page_table_action_function(struct hash_elem *a_, void *aux UNUSED){
  struct page_table_entry *a = hash_entry(a_, struct page_table_entry, hash_elem);
  free(a);
}


/* init page table */
void page_table_init(struct hash *pt){
  hash_init(pt, page_table_hash, page_table_less, NULL);
}

/* destroy page table */
void page_table_destroy(struct hash *pt){
  hash_destroy(pt, page_table_action_function);
}

/* add entry of page table. If done, return true */
bool page_table_add_entry(struct file *file, off_t ofs, uint8_t *upage, size_t page_read_bytes, size_t page_zero_bytes, bool writable){
  struct page_table_entry *pte = malloc(sizeof(struct page_table_entry));
  void *uaddr = pg_round_down(upage);
  pte->file = file;
  pte->ofs = ofs;
  pte->upage = uaddr;
  pte->page_read_bytes = page_read_bytes;
  pte->page_zero_bytes = page_zero_bytes;
  pte->writable = writable;
  pte->valid = false;
  pte->is_swapped = false;
  if(hash_insert(&thread_current()->spt, &pte->hash_elem) == NULL){
    return true;
  }
  else{
    return false;
  }
}

/* Returns the page containing the given virtual address,
or a null pointer if no such page exists. */
struct page_table_entry *page_table_lookup_by_upage(void *upage){
  struct page_table_entry p;
  struct hash_elem *e;
  p.upage = upage;
  e = hash_find(&thread_current()->spt, &p.hash_elem);
  return e != NULL ? hash_entry(e, struct page_table_entry, hash_elem) : NULL;
}

/* for page fault handling */
bool page_fault_handler(void *uaddr){
  void *upage = pg_round_down(uaddr);
  struct page_table_entry *pte = page_table_lookup_by_upage(upage);
  //if no pte
  if(pte == NULL){
    /*
    //grow stack case
    if(){

    }
    //no valid access
    else{

    }
    */
  }
  //if pte is in page table
  else{
    //if pte is valid
    if(pte->valid){
      //if pte is swapped
      if(pte->is_swapped){
        return page_load_swap(pte);
      }
      //if pte is not swapped
      else{
        return true;
      }
    }
    //if pte is invalid
    else{
      return page_load_file(pte);
    }
  }
}

/* load page from file */
bool page_load_file(struct page_table_entry *pte){
  uint8_t *kpage = frame_get_page (PAL_USER|PAL_ZERO);
  if (kpage == NULL)
    return false;

  if (file_read_at(pte->file, kpage, pte->page_read_bytes, pte->ofs) != (int) pte->page_read_bytes)
    {
      palloc_free_page (kpage);
      return false;
    }
  memset (kpage + pte->page_read_bytes, 0, pte->page_zero_bytes);

  if (!install_page (pte->upage, kpage, pte->writable))
    {
      palloc_free_page (kpage);
      return false;
    }

  //add frame table
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
  fte->paddr = kpage;
  fte->pte = pte;
  list_push_back(&frame_table, &fte->elem);

  //change state of pte
  pte->valid = true;
  pte->fte = fte;
  return true;
}

/* load page from swap disk */
bool page_load_swap(struct page_table_entry *pte){
  uint8_t *kpage = frame_get_page(PAL_USER|PAL_ZERO);
  swap_in(pte->sector_index, kpage);
  if(!install_page(pte->upage, kpage, pte->writable)){
    palloc_free_page(kpage);
    return false;
  }

  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
  fte->paddr = kpage;
  fte->pte = pte;
  list_push_back(&frame_table, &fte->elem);

  pte->is_swapped = false;
  pte->fte = fte;
  return true;
}
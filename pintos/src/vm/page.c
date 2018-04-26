#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

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
  //void *upage = pg_round_down(uaddr);
}

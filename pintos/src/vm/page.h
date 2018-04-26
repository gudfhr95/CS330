#include <hash.h>
#include <debug.h>
#include "filesys/off_t.h"
#include "vm/swap.h"

struct page_table_entry{
  struct file *file;
  off_t ofs;
  uint8_t *upage;
  size_t page_read_bytes;
  size_t page_zero_bytes;
  bool writable;

  /* for demand paging */
  bool valid;
  bool is_swapped;
  block_sector_t sector_index;

  struct frame_table_entry *fte;

  struct hash_elem hash_elem;
};

/* for hash table */
unsigned page_table_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_table_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
void page_table_action_function(struct hash_elem *a_, void *aux UNUSED);

/* for managing page table */
void page_table_init(struct hash *pt);
void page_table_destroy(struct hash *pt);
bool page_table_add_entry(struct file *file, off_t ofs, uint8_t *upage, size_t page_read_bytes, size_t page_zero_bytes, bool writable);
struct page_table_entry *page_table_lookup_by_upage(void *upage);

/* handling page fault */
bool page_fault_handler(void *upage);

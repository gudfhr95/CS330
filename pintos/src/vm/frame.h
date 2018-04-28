#include <list.h>
#include "threads/palloc.h"

struct list frame_table;


struct frame_table_entry{
  void *paddr;
  struct page_table_entry *pte;

  struct list_elem elem;
};


/* for managing frame table */
void frame_table_init(void);
void *frame_get_page(enum palloc_flags flags);
//void *frame_free_page(void *frame);
struct frame_table_entry *frame_find_victim(void);
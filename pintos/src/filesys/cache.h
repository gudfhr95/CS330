#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"

#define MAX_CACHE_SIZE 64
#define WRITE_BEHIND_PERIOD 50

struct list cache;

struct lock cache_lock;

struct cache_entry{
  uint8_t data[BLOCK_SECTOR_SIZE];
  block_sector_t sector_index;
  bool valid;
  bool dirty;
  struct list_elem elem;
};



/* for cache management */
void cache_init(void);
void cache_get_block(block_sector_t index);
void cache_read(block_sector_t index, void *buffer);
void cache_write(block_sector_t index, const void *buffer);

/* for cache searching */
struct cache_entry *cache_find_block(block_sector_t index);
struct cache_entry *cache_find_victim(void);


/* for write behind */
void thread_func_write_behind(void *aux);

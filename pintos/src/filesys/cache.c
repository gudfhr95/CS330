#include "filesys/cache.h"
#include <string.h>
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/filesys.h"


/* init cache and lock */
void cache_init(void){
  list_init(&cache);
  lock_init(&cache_lock);
  thread_create("cache_write_behind", 0, thread_func_write_behind, NULL);
}


/* get cache block
  if cache is full, evict */
void cache_get_block(block_sector_t index){
  size_t cache_size = list_size(&cache);
  //if cache is full -> remove victim
  if(cache_size >= MAX_CACHE_SIZE){
    struct cache_entry *victim = cache_find_victim();
    if(victim->dirty){
      block_write(fs_device, victim->sector_index, &victim->data);
    }
    list_remove(&victim->elem);
    free(victim);
  }
  struct cache_entry *c = malloc(sizeof(struct cache_entry));
  block_read(fs_device, index, &c->data);
  c->sector_index = index;
  c->valid = false;
  c->dirty = false;
  list_push_front(&cache, &c->elem);
}


/* read data from block
  if cache is available, read from cache
  else, make cache */
void cache_read(block_sector_t index, void *buffer){
  struct cache_entry *c = cache_find_block(index);
  // if cache available
  if(c){
    memcpy(buffer, &c->data, BLOCK_SECTOR_SIZE+1);
  }
  // if no cache
  else{
    cache_get_block(index);
    memcpy(buffer, &c->data, BLOCK_SECTOR_SIZE+1);
  }
}


/* write data to block
  if cache is available, write data to cache
  else, make cache */
void cache_write(block_sector_t index, const void *buffer){
  struct cache_entry *c = cache_find_block(index);
  // if cache available
  if(c){
    memcpy(&c->data, buffer, BLOCK_SECTOR_SIZE+1);
    c->dirty = true;
  }
  // if no cache
  else{
    cache_get_block(index);
    memcpy(&c->data, buffer, BLOCK_SECTOR_SIZE+1);
    c->dirty = true;
  }
}


/* find cache by block index
  if there is no cache block, return NULL */
struct cache_entry *cache_find_block(block_sector_t index){
  struct list_elem *e;
  for(e=list_begin(&cache); e!=list_end(&cache); e=list_next(e)){
    struct cache_entry *c = list_entry(e, struct cache_entry, elem);
    if(c->sector_index == index){
      return c;
    }
  }
  return NULL;
}


/* get victim of cache */
// FIFO
struct cache_entry *cache_find_victim(void){
  struct list_elem *e = list_pop_back(&cache);
  struct cache_entry *victim = list_entry(e, struct cache_entry, elem);
  return victim;
}


/* for write behind thread */
void thread_func_write_behind(void *aux UNUSED){
  while(true){
    timer_sleep(WRITE_BEHIND_PERIOD); //sleep
    //synchronize dirty cache
    struct list_elem *e;
    for(e=list_begin(&cache); e!=list_end(&cache); e=list_next(e)){
      struct cache_entry *c = list_entry(e, struct cache_entry, elem);
      if(c->dirty){
        block_write(fs_device, c->sector_index, &c->data);
        c->dirty = false;
      }
    }
  }
}

#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"

struct block *swap_block;

struct lock swap_lock;

struct bitmap *swap_bitmap;

/* for managing swap */
void swap_init(void);
void swap_in(block_sector_t index, void *paddr);
block_sector_t swap_out(void *paddr);

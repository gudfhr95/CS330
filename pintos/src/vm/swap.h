#include <bitmap.h>
#include "devices/block.h"

struct block *swap_block;

struct bitmap *swap_bitmap;

void swap_init(void);
void swap_in(block_sector_t index, void *paddr);
block_sector_t swap_out(void *paddr);

#include "vm/frame.h"


/* init frame table */
void frame_table_init(void){
  list_init(&frame_table);
}

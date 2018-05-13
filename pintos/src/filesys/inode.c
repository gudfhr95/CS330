#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAX_DIRECT_BLOCK 12
#define MAX_INDIRECT_BLOCK 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

    block_sector_t direct_ptr[MAX_DIRECT_BLOCK];   /* sector number of direct blocks */
    block_sector_t indirect_ptr;      /* pointer sector number of indirect block */
    block_sector_t double_indirect_ptr;   /* pointer sector number of double indirect block */

    bool dir;                       /* indicate whether inode is dir or not */
    block_sector_t parent;          /* parent sector number of dir */

    uint32_t unused[110];               /* Not used. */
  };

struct indirect_disk{
  block_sector_t block_ptr[MAX_INDIRECT_BLOCK];
};

struct double_indirect_disk{
  block_sector_t indirect_ptr[MAX_INDIRECT_BLOCK];
};

/* for inode allocation and freeing */
bool inode_alloc(struct inode_disk *disk_inode);
bool inode_alloc_direct(struct inode_disk *disk_inode, block_sector_t sectors);
bool inode_alloc_indirect(struct inode_disk *disk_inode, block_sector_t sectors);
bool inode_alloc_double_indirect(struct inode_disk *disk_inode, block_sector_t sectors);
void inode_free(struct inode *inode);

/* for inode growth */
void inode_grow(struct inode *inode, off_t size);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;

    size_t direct_cnt;                  /* number of direct block */
    size_t indirect_cnt;                /* number of indirect direct block */
    size_t double_indirect_cnt;         /* number of double indirect direct block */

    bool dir;                           /* indicate whether inode is dir or not */
    block_sector_t parent;              /* parent sector number of dir */

    struct lock inode_lock;              /* lock of inode */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  block_sector_t block_ptr[MAX_INDIRECT_BLOCK];     // for indirect case
  block_sector_t indirect_ptr[MAX_INDIRECT_BLOCK];  // for double indirect case

  if (pos < inode->data.length){
    block_sector_t sectors = pos / BLOCK_SECTOR_SIZE;
    //direct
    if(sectors < MAX_DIRECT_BLOCK){
      return inode->data.direct_ptr[sectors];
    }
    //indirect
    else if(sectors < (MAX_DIRECT_BLOCK + MAX_INDIRECT_BLOCK)){
      int diff = sectors - MAX_DIRECT_BLOCK;  // get offset at indirect
      block_read(fs_device, inode->data.indirect_ptr, &block_ptr);  // get indirect
      return block_ptr[diff];
    }
    //double indirect
    else{
      int diff = sectors - (MAX_INDIRECT_BLOCK + MAX_DIRECT_BLOCK); // get offset at double indirect
      block_read(fs_device, inode->data.double_indirect_ptr, &indirect_ptr);   // get indirect
      int indirect_idx = diff / MAX_INDIRECT_BLOCK; // get index of indirect at double indirect
      block_read(fs_device, indirect_ptr[indirect_idx], &block_ptr);  //get indirect
      int block_idx = diff % MAX_INDIRECT_BLOCK;  // get offset in indirect
      return block_ptr[block_idx];
    }
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool dir, block_sector_t parent)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->dir = dir;
      disk_inode->parent = parent;
      // allocate disk_inode
      if (inode_alloc(disk_inode))
        {
          block_write (fs_device, sector, disk_inode);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}


/* allocate inode disk in filesys disk */
bool inode_alloc(struct inode_disk *disk_inode){
  size_t sectors = bytes_to_sectors (disk_inode->length);
  // direct alloc
  if(sectors <= MAX_DIRECT_BLOCK){
    if(!inode_alloc_direct(disk_inode, sectors)){
        return false;
    }
  }
  // indirect alloc
  else if((MAX_DIRECT_BLOCK < sectors) && (sectors <= (MAX_DIRECT_BLOCK + MAX_INDIRECT_BLOCK))){
    // do direct alloc first
    if(!inode_alloc_direct(disk_inode, MAX_DIRECT_BLOCK)){
      return false;
    }
    // do indirect alloc
    sectors -= MAX_DIRECT_BLOCK;
    if(!inode_alloc_indirect(disk_inode, sectors + 1)){
      return false;
    }
  }
  // double indirect alloc
  else{
    // do direct alloc first
    if(!inode_alloc_direct(disk_inode, MAX_DIRECT_BLOCK)){
      return false;
    }
    // do indirect alloc
    sectors -= MAX_DIRECT_BLOCK;
    if(!inode_alloc_indirect(disk_inode, MAX_INDIRECT_BLOCK)){
      return false;
    }
    // do double indirect alloc
    sectors -= MAX_INDIRECT_BLOCK;
    if(!inode_alloc_double_indirect(disk_inode, sectors)){
      return false;
    }
  }
  return true;
}

/* direct alloc */
bool inode_alloc_direct(struct inode_disk *disk_inode, block_sector_t sectors){
  static char zeros[BLOCK_SECTOR_SIZE];
  unsigned i;
  for(i=0; i<sectors; i++){
    if(!free_map_allocate(1, &disk_inode->direct_ptr[i])){
      return false;
    }
    block_write(fs_device, disk_inode->direct_ptr[i], zeros);
  }
  return true;
}

/* indirect alloc */
bool inode_alloc_indirect(struct inode_disk *disk_inode, block_sector_t sectors){
  static char zeros[BLOCK_SECTOR_SIZE];
  struct indirect_disk *id = malloc(sizeof(struct indirect_disk)); // make indirect block
  // allocate along indirect block
  unsigned i;
  for(i=0; i<sectors; i++){
    if(!free_map_allocate(1, &id->block_ptr[i])){
      return false;
    }
    block_write(fs_device, id->block_ptr[i], zeros);
  }
  if(!free_map_allocate(1, &disk_inode->indirect_ptr)){  // alloc blocks for indirect
    return false;
  }
  block_write(fs_device, disk_inode->indirect_ptr, id); // write indirect block
  free(id);
  return true;
}

/* double indirect alloc */
bool inode_alloc_double_indirect(struct inode_disk *disk_inode, block_sector_t sectors){
  static char zeros[BLOCK_SECTOR_SIZE];
  block_sector_t indirect_cnt = (sectors / MAX_INDIRECT_BLOCK) + 1;
  struct double_indirect_disk *did = malloc(sizeof(struct double_indirect_disk));  // make double indirect block
  // iterate through indirect_cnt
  unsigned i;
  for(i=0; i<indirect_cnt; i++){
    // if sector is less then indirect block
    if(sectors < MAX_INDIRECT_BLOCK){
      struct indirect_disk *id = malloc(sizeof(struct indirect_disk)); // make indirect block
      unsigned j;
      for(j=0; j<sectors; j++){
        if(!free_map_allocate(1, &id->block_ptr[j])){
          return false;
        }
        block_write(fs_device, id->block_ptr[j], zeros);
      }
      if(!free_map_allocate(1, &did->indirect_ptr[i])){  // alloc blocks for indirect in double indirect block
        return false;
      }
      block_write(fs_device, did->indirect_ptr[i], id); // write indirect block
      free(id);
    }
    else{
      struct indirect_disk *id = malloc(sizeof(struct indirect_disk)); // make indirect block
      // allocate along indirect block
      unsigned j;
      for(j=0; j<MAX_INDIRECT_BLOCK; j++){
        if(!free_map_allocate(1, &id->block_ptr[j])){
          return false;
        }
        block_write(fs_device, id->block_ptr[j], zeros);
      }
      if(!free_map_allocate(1, &did->indirect_ptr[i])){  // alloc blocks for indirect in double indirect block
        return false;
      }
      block_write(fs_device, did->indirect_ptr[i], id); // write indirect block
      free(id);
    }
    sectors -= MAX_INDIRECT_BLOCK;
  }

  if(!free_map_allocate(1, &disk_inode->double_indirect_ptr)){  // alloc blocks for double indirect
    return false;
  }
  block_write(fs_device, disk_inode->double_indirect_ptr, did); // write double indirect block
  free(did);

  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  // init counts
  size_t sectors = bytes_to_sectors(inode->data.length);
  // direct case
  if(sectors <= MAX_DIRECT_BLOCK){
    inode->direct_cnt = sectors;
    inode->indirect_cnt = 0;
    inode->double_indirect_cnt = 0;
  }
  // indirect case
  else if(sectors <= MAX_DIRECT_BLOCK + MAX_INDIRECT_BLOCK){
    inode->direct_cnt = MAX_DIRECT_BLOCK;
    inode->indirect_cnt = (sectors - MAX_DIRECT_BLOCK);
    inode->double_indirect_cnt = 0;
  }
  // double indirect case
  else{
    inode->direct_cnt = MAX_DIRECT_BLOCK;
    inode->indirect_cnt = MAX_INDIRECT_BLOCK;
    inode->double_indirect_cnt = (sectors - MAX_DIRECT_BLOCK - MAX_INDIRECT_BLOCK);
  }

  inode->dir = inode->data.dir;
  inode->parent = inode->data.parent;

  lock_init(&inode->inode_lock);

  return inode;
}


/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          inode_free(inode);
        }
      // save inode data to disk in cache
      else{
        //synch
        struct list_elem *e;
        for(e=list_begin(&cache); e!=list_end(&cache); e=list_next(e)){
          struct cache_entry *c = list_entry(e, struct cache_entry, elem);
          if(inode->sector == c->sector_index){
            if(c->dirty){
              block_write(fs_device, c->sector_index, &c->data);
            }
            list_remove(&c->elem);
            free(c);
            break;
          }
        }


      }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}


/* free inode disk */
void inode_free(struct inode *inode){
  block_sector_t block_ptr[MAX_INDIRECT_BLOCK];     // for indirect case
  block_sector_t indirect_ptr[MAX_INDIRECT_BLOCK];  // for double indirect case
  unsigned i, j;
  // direct
  for(i=0; i<inode->direct_cnt; i++){
    free_map_release(inode->data.direct_ptr[i], 1);
  }
  // indirect
  block_read(fs_device, inode->data.indirect_ptr, block_ptr); // read indirect block ptr
  for(i=0; i<inode->indirect_cnt; i++){
    free_map_release(block_ptr[i], 1);
  }

  // dobule indirect
  unsigned indirects = (inode->double_indirect_cnt / MAX_INDIRECT_BLOCK) + 1;
  block_read(fs_device, inode->data.double_indirect_ptr, indirect_ptr);
  for(i=0; i<indirects; i++){
    block_read(fs_device, indirect_ptr[i], block_ptr);
    if(inode->double_indirect_cnt < MAX_INDIRECT_BLOCK){
      for(j=0; j<inode->double_indirect_cnt; j++){
        free_map_release(block_ptr[i], 1);
      }
    }
    else{
      for(j=0; j<MAX_INDIRECT_BLOCK; j++){
        free_map_release(block_ptr[i], 1);
      }
    }
    inode->double_indirect_cnt -= MAX_INDIRECT_BLOCK;
  }
  free_map_release(inode->sector, 1);
}


/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;


      //printf("READ IDX : %d\n", sector_idx);
      struct cache_entry *c = cache_find_block(sector_idx); //get cache
      if(!c){
        c = cache_get_block(sector_idx);  // if no cache, allocate new cache
      }
      memcpy(buffer + bytes_read, (uint8_t *)&c->data + sector_ofs, chunk_size);  //read data from cache

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if (offset + size > inode_length(inode)){
    // if inode is file, acquire lock
    if(!inode->dir){
      inode_lock_acquire(inode);
      inode_grow(inode, size + offset);
      //size growth
      inode->data.length = size + offset;
      block_write(fs_device, inode->sector, &inode->data);
      inode_lock_release(inode);
    }
    else{
      inode_grow(inode, size + offset);
      //size growth
      inode->data.length = size + offset;
      block_write(fs_device, inode->sector, &inode->data);
    }
  }


  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      //printf("WRITE IDX : %d\n", sector_idx);
      struct cache_entry *c = cache_find_block(sector_idx); //get cache
      if(!c){
        c = cache_get_block(sector_idx);  // if no cache, allocate new cache
      }
      memcpy ((uint8_t *) &c->data + sector_ofs, buffer + bytes_written, chunk_size); // write data to cache
      c->dirty = true;

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* grow inode until size t */
void inode_grow(struct inode *inode, off_t size){
  static char zeros[BLOCK_SECTOR_SIZE];
  block_sector_t block_ptr[MAX_INDIRECT_BLOCK];     // for indirect case
  block_sector_t indirect_ptr[MAX_INDIRECT_BLOCK];  // for double indirect case
  size = bytes_to_sectors(size);
  while(size > 0){
    //indirect growth case
    if(inode->direct_cnt < MAX_DIRECT_BLOCK){
      free_map_allocate(1, &inode->data.direct_ptr[inode->direct_cnt]);
      block_write(fs_device, inode->data.direct_ptr[inode->direct_cnt], zeros);
      //printf("DIRECT PTR : %d\n", inode->data.direct_ptr[inode->direct_cnt]);
      inode->direct_cnt++;
    }
    //indirect growth case
    else if(inode->indirect_cnt < MAX_INDIRECT_BLOCK){
      // if indirect == 0, we have to allocate new block
      if(inode->indirect_cnt == 0){
        free_map_allocate(1, &inode->data.indirect_ptr);
        block_write(fs_device, inode->data.indirect_ptr, zeros);
      }
      block_read(fs_device, inode->data.indirect_ptr, block_ptr);  // read block ptrs in indirect block
      free_map_allocate(1, &block_ptr[inode->indirect_cnt]);
      block_write(fs_device, block_ptr[inode->indirect_cnt], zeros);  // write single block ptr
      block_write(fs_device, inode->data.indirect_ptr, block_ptr);  // write indirect block
      inode->indirect_cnt++;
    }
    //double indirect growth case
    else{
      unsigned indirect_idx = inode->double_indirect_cnt / MAX_INDIRECT_BLOCK;
      unsigned block_idx = inode->double_indirect_cnt % MAX_INDIRECT_BLOCK;
      // if double_indirect == 0, we have to allocate new block
      if(inode->double_indirect_cnt == 0){
        free_map_allocate(1, &inode->data.double_indirect_ptr);
        block_write(fs_device, inode->data.double_indirect_ptr, zeros);
      }
      // allocate new indirect block
      if(inode->double_indirect_cnt % MAX_INDIRECT_BLOCK == 0){
        block_read(fs_device, inode->data.double_indirect_ptr, indirect_ptr);
        free_map_allocate(1, &indirect_ptr[indirect_idx]);
        block_write(fs_device, indirect_ptr[indirect_idx], zeros);
        block_write(fs_device, inode->data.double_indirect_ptr, indirect_ptr);
      }
      block_read(fs_device, inode->data.double_indirect_ptr, indirect_ptr);
      block_read(fs_device, indirect_ptr[indirect_idx], block_ptr);
      free_map_allocate(1, &block_ptr[block_idx]);
      block_write(fs_device, block_ptr[block_idx], zeros);
      block_write(fs_device, indirect_ptr[indirect_idx], block_ptr);
      block_write(fs_device, inode->data.double_indirect_ptr, indirect_ptr);
      inode->double_indirect_cnt++;
    }
    size -= 1;
  }
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Returns the dir of inode */
bool
inode_get_dir (const struct inode *inode)
{
  return inode->dir;
}

/* Returns sector of inode */
block_sector_t inode_get_sector(const struct inode *inode){
  return inode->sector;
}

/* Returns parent sector of indoe */
block_sector_t inode_get_parent_sector(const struct inode *inode){
  return inode->parent;
}

/* Returns open_cont of inode */
int inode_get_open_cnt(const struct inode *inode){
  return inode->open_cnt;
}

/* acquire lock of inode */
void inode_lock_acquire(const struct inode *inode){
  lock_acquire(&((struct inode *)inode)->inode_lock);
}

/* release lock of inode */
void inode_lock_release(const struct inode *inode){
  lock_release(&((struct inode *)inode)->inode_lock);
}

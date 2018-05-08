#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/malloc.h"
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
    block_sector_t start;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

    block_sector_t direct_ptr[MAX_DIRECT_BLOCK];   /* sector number of direct blocks */
    block_sector_t indirect_ptr;      /* pointer sector number of indirect block */
    block_sector_t double_indirect_ptr;   /* pointer sector number of double indirect block */

    uint32_t unused[111];               /* Not used. */
  };

struct indirect_disk{
  block_sector_t block_ptr[MAX_INDIRECT_BLOCK];
};

struct double_indirect_disk{
  block_sector_t indirect_ptr[MAX_INDIRECT_BLOCK];
};

/* for inode allocation */
bool inode_alloc(struct inode_disk *disk_inode);
bool inode_alloc_direct(struct inode_disk *disk_inode, block_sector_t sectors);
bool inode_alloc_indirect(struct inode_disk *disk_inode, block_sector_t sectors);
bool inode_alloc_double_indirect(struct inode_disk *disk_inode, block_sector_t sectors);

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
    //direct
    if(pos < MAX_DIRECT_BLOCK * BLOCK_SECTOR_SIZE){
      return inode->data.direct_ptr[pos/BLOCK_SECTOR_SIZE];
    }
    //indirect
    else if(MAX_DIRECT_BLOCK * BLOCK_SECTOR_SIZE <= pos && pos < (MAX_DIRECT_BLOCK + MAX_INDIRECT_BLOCK) * BLOCK_SECTOR_SIZE){
      int temp = pos - (MAX_DIRECT_BLOCK * BLOCK_SECTOR_SIZE);  // get offset at indirect
      block_read(fs_device, inode->data.indirect_ptr, &block_ptr);  // get indirect
      int idx = temp / BLOCK_SECTOR_SIZE; // get index in indirect

      return block_ptr[idx];
    }
    //double indirect
    else{
      int temp = pos - ((MAX_INDIRECT_BLOCK + MAX_DIRECT_BLOCK) * BLOCK_SECTOR_SIZE); // get offset at double indirect
      block_read(fs_device, inode->data.double_indirect_ptr, &indirect_ptr);   // get indirect
      int indirect_idx = temp / BLOCK_SECTOR_SIZE * MAX_INDIRECT_BLOCK; // get index of indirect at double indirect
      block_read(fs_device, indirect_ptr[indirect_idx], &block_ptr);  //get indirect
      int temp2 = temp % BLOCK_SECTOR_SIZE * MAX_INDIRECT_BLOCK;  // get offset in indirect
      int block_idx = temp2 / BLOCK_SECTOR_SIZE;  // get index of block in indirect

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
inode_create (block_sector_t sector, off_t length)
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
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
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
    if(!inode_alloc_indirect(disk_inode, sectors)){
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
    if(!inode_alloc_indirect(disk_inode, sectors)){
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
      unsigned i;
      for(i=0; i<sectors; i++){
        if(!free_map_allocate(1, &id->block_ptr[i])){
          return false;
        }
        block_write(fs_device, id->block_ptr[i], zeros);
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
      unsigned i;
      for(i=0; i<MAX_INDIRECT_BLOCK; i++){
        if(!free_map_allocate(1, &id->block_ptr[i])){
          return false;
        }
        block_write(fs_device, id->block_ptr[i], zeros);
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

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read(sector_idx, buffer + bytes_read);
        }
      else
        {
          struct cache_entry *c = cache_find_block(sector_idx); //get cache
          if(!c){
            c = cache_get_block(sector_idx);  // if no cache, allocate new cache
          }
          memcpy(buffer + bytes_read, (uint8_t *)&c->data + sector_ofs, chunk_size);  //read data from cache
        }

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

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write(sector_idx, buffer + bytes_written);
        }
      else
        {
          struct cache_entry *c = cache_find_block(sector_idx); //get cache
          if(!c){
            c = cache_get_block(sector_idx);  // if no cache, allocate new cache
          }
          memcpy ((uint8_t *) &c->data + sector_ofs, buffer + bytes_written, chunk_size); // write data to cache
          c->dirty = true;
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
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

#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  //synch
  struct list_elem *e;
  for(e=list_begin(&cache); e!=list_end(&cache); e=list_next(e)){
    struct cache_entry *c = list_entry(e, struct cache_entry, elem);
    if(c->dirty){
      block_write(fs_device, c->sector_index, &c->data);
    }
  }

  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  // absolute path
  if(name[0] == '/'){
    dir = dir_absolute_path(name);
  }
  // relative path
  else{
    dir = dir_relative_path(name);
  }

  // get name of file
  char temp_path[FILE_NAME_MAX_LENGTH];
  char *argv[ARGS_MAX_LENGTH];
  int argc=0;
  strlcpy(temp_path, name, FILE_NAME_MAX_LENGTH);
  parse_dir_path(temp_path, argv, &argc);

  struct inode *parent = dir_get_inode(dir);
  block_sector_t parent_sector = inode_get_sector(parent);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false, parent_sector)
                  && dir_add (dir, argv[argc-1], inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = NULL;
  // absolute path
  if(name[0] == '/'){
    dir = dir_absolute_path(name);
  }
  // relative path
  else{
    dir = dir_relative_path(name);
  }

  // get name of file
  char temp_path[FILE_NAME_MAX_LENGTH];
  char *argv[ARGS_MAX_LENGTH];
  int argc=0;
  strlcpy(temp_path, name, FILE_NAME_MAX_LENGTH);
  parse_dir_path(temp_path, argv, &argc);


  struct inode *inode = NULL;
  // if argc is 0, open root dir
  if(argc == 0){
    inode = inode_open(ROOT_DIR_SECTOR);
    dir_close (dir);
    return file_open (inode);
  }
  else{
    if (dir != NULL){
      // if opening current directory
      if(!strcmp(argv[argc-1], ".")){
        inode = dir_get_inode(dir);
        //dir_close(dir);
        return file_open(inode);
      }
      else{
        dir_lookup (dir, argv[argc-1], &inode);
      dir_close (dir);
      return file_open (inode);
      }
    }
    else{
      return NULL;
    }
  }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  // remove root directory
  if(!strcmp(name, "/")){
    return false;
  }

  struct dir *dir = NULL;
  // absolute path
  if(name[0] == '/'){
    dir = dir_absolute_path(name);
  }
  // relative path
  else{
    dir = dir_relative_path(name);
  }

  // get name of file
  char temp_path[FILE_NAME_MAX_LENGTH];
  char *argv[ARGS_MAX_LENGTH];
  int argc=0;
  strlcpy(temp_path, name, FILE_NAME_MAX_LENGTH);
  parse_dir_path(temp_path, argv, &argc);

  bool success = dir != NULL && dir_remove (dir, argv[argc-1]);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

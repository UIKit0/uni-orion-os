#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>

#include "userprog/process.h"

#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/path.h"

#ifdef FILESYS_SYNC
#include "threads/synch.h"
#endif

/**
 * In-memory representation of a directory. 
 */
struct dir 
{
    struct inode *inode;    /* Backing store. */
    off_t pos;              /* Current position. */
};

/**
 *  Disk representation of a directory entry.
 */
struct dir_entry 
{
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool is_directory;                  /* Type of the entry. */
    bool in_use;                        /* In use or free? */
};

// struct dir_list_elem {
//     struct list_elem elem;
//     struct dir *dir;
// };

static struct dir *root_dir;
// static struct list open_dirs;

void dir_init() {
    root_dir = dir_open(inode_open(ROOT_DIR_SECTOR));
    // list_init(&open_dirs);
}

/**
 * Creates a directory with space for ENTRY_CNT entries in the given SECTOR.
 * Returns true if successful, false on failure. 
 */
bool dir_create (block_sector_t sector, size_t entry_count, block_sector_t parent)
{
  #ifdef FILESYS_SYNC
	inode_global_lock();
  #endif

	bool success;

  #ifdef FILESYS_SUBDIRS
    success = inode_create(sector, entry_count * sizeof(struct dir_entry), parent);
  #else
    success = inode_create(sector, entry_count * sizeof(struct dir_entry));
  #endif

  #ifdef FILESYS_SYNC
    inode_global_unlock();
  #endif

    return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *dir_open (struct inode *inode) 
{
#ifdef FILESYS_SYNC
	inode_lock(inode);
#endif

    struct list_elem *e;

    /* Check whether this dir is already open. */
    // for (e = list_begin (&open_dirs); e != list_end (&open_dirs); e = list_next (e)) {
    //     struct dir_list_elem *elem = list_entry (e, struct dir_list_elem, elem);
    //     if (elem->dir->inode == inode) {
    //         return elem->dir;
    //     }
    // }

    struct dir *dir = calloc (1, sizeof *dir);
    if (inode != NULL && dir != NULL)
    {
        dir->inode = inode;
        dir->pos = 0;
        // struct dir_list_elem elem = (dir_list_elem*)malloc(sizeof struct dir_list_elem);
        // elem->dir = dir;
        // list_push_front(&open_dirs, &elem->elem);
	#ifdef FILESYS_SYNC
        inode_unlock(inode);
	#endif
        return dir;
    }
    else
    {
	#ifdef FILESYS_SYNC
        inode_unlock(inode);
	#endif
        inode_close (inode);
        free (dir);
        return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *dir_open_root (void)
{
    struct inode *root_inode = inode_open(ROOT_DIR_SECTOR);
    return dir_open(root_inode);
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *dir_reopen (struct dir *dir) 
{
    return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close (struct dir *dir) 
{
    if (dir != NULL)
    {
        inode_close (&(dir->inode));
        free (dir);
    }

}

/* Returns the inode encapsulated by DIR. */
struct inode *dir_get_inode (struct dir *dir) 
{
    return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup (const struct dir *dir, const char *name, struct dir_entry *ep, off_t *ofsp) 
{
    struct dir_entry e;
    size_t ofs;

    ASSERT (dir != NULL);
    ASSERT (name != NULL);

    size_t entry_size = sizeof e;

    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == entry_size; ofs += entry_size) 
    {
        if (e.in_use && !strcmp (name, e.name)) 
        {
            if (ep != NULL) *ep = e;
            if (ofsp != NULL) *ofsp = ofs;
            return true;
        }
    }

    return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup (const struct dir *dir, const char *name, struct inode **inode) 
{
    struct dir_entry e;

    ASSERT (dir != NULL);
    ASSERT (name != NULL);

    if (lookup (dir, name, &e, NULL))
    {
        *inode = inode_open (e.inode_sector);
    } else {
        *inode = NULL;
    }

    return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
 #ifdef FILESYS_SYNC
  inode_lock(dir->inode);
 #endif
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
  {
#ifdef FILESYS_SYNC
    inode_unlock(dir->inode);
#endif
    return false;
  }

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
  {
#ifdef FILESYS_SYNC
    inode_unlock(dir->inode);
#endif
    goto done;
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  // printf("Adding entry %s to dir, which is a %d", name, is_dir);
  e.in_use = true;
  e.is_directory = is_dir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
#ifdef FILESYS_SYNC
  inode_unlock(dir->inode);
#endif

  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
#ifdef FILESYS_SYNC
  inode_lock(dir->inode);
#endif
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
#ifdef FILESYS_SYNC
  inode_unlock(dir->inode);
#endif
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;
#ifdef FILESYS_SYNC
  inode_lock(dir->inode);
#endif
  /* Remove inode. */
  inode_remove (inode);
  success = true;
#ifdef FILESYS_SYNC
  inode_unlock(dir->inode);
#endif
 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
#ifdef FILESYS_SYNC
  inode_lock(dir->inode);
#endif
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
	 #ifdef FILESYS_SYNC
        inode_unlock(dir->inode);
	 #endif
          return true;
        }
    }
#ifdef FILESYS_SYNC
  inode_unlock(dir->inode);
#endif
  return false;
}

#ifdef FILESYS_SUBDIRS
struct inode *dir_open_from_path(const char *path, bool *is_dir) {
    // printf("[debug] opening path %s\n", path);
    // printf("root dir: %p %d\n", root_dir, inode_get_inumber(dir_get_inode(root_dir)));

    if (strcmp(path, "/") == 0) {
      *is_dir = true;
      return dir_get_inode(root_dir);
    }

    if (strcmp(path, ".") == 0) {
      *is_dir = true;
      return dir_get_inode(process_working_directory(process_current()));
    }

    const struct dir *current_dir;
    char entry_name_buffer[NAME_MAX + 1];
    int offset = 0;
    struct dir_entry dir_entry_buffer;

    // initialize the current directory
    if (path_is_relative(path)) {
        current_dir = process_working_directory(process_current());
        // printf("[debug] path is relative to %x\n", inode_get_inumber(current_dir->inode));
    } else {
        current_dir = root_dir;
        // printf("[debug] path is absolute: %d\n", inode_get_inumber(current_dir->inode));
    }

    // walk the path
    while (*(path + offset) != '\0') {
        path_next_entry(path + offset, entry_name_buffer, NAME_MAX + 1, &offset);

        // handle parent directory
        if (strcmp(entry_name_buffer, "..") == 0) {
            struct dir *old_dir = current_dir;
            current_dir = dir_parent(current_dir);
            if (old_dir != root_dir) {
              free(old_dir);
            }
        } else {
            // handle subdirectory
            bool found = lookup(current_dir, entry_name_buffer, &dir_entry_buffer, NULL);

            if (!found) {
               // printf("[debug] Could not find entry %s.\n", entry_name_buffer);
            }

            if (found) {
                // printf("[debug] found entry %s at inode %d.\n", entry_name_buffer, dir_entry_buffer.inode_sector);
                if (dir_entry_buffer.is_directory) {
                    struct dir *old_dir = current_dir;
                    struct inode *inode = inode_open(dir_entry_buffer.inode_sector);
                    // printf("[debug] directory entry at inode %d.\n", inode_get_inumber(inode));
                    current_dir = dir_open(inode);
                    // printf("[debug] dir pointer created at %p.\n", current_dir);
                    // if (old_dir != root_dir) {
                    //   free(old_dir);
                    // }
                } else {
                    path_next_entry(path + offset, entry_name_buffer, NAME_MAX + 1, &offset);
                    if (entry_name_buffer[0] == '\0') {
                        *is_dir = false;
                        return inode_open(dir_entry_buffer.inode_sector);
                    }
                }
            } else {
                return NULL;
            }
        }
    }
    // printf("[debug] exiting %p %d\n", current_dir, inode_get_inumber(current_dir->inode));
    *is_dir = true;
    return current_dir->inode;
}

struct dir *dir_parent(const struct dir *dir) {
    if (dir == root_dir) {
        return root_dir;
    } else {
        return dir_open(inode_parent(dir->inode));
    }
}

struct dir *dir_parent_from_inode(const struct inode *inode) {
  return dir_open(inode_parent(inode));
}
#endif

#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/path.h"
#include "threads/malloc.h"
#ifdef FILESYS_USE_CACHE
#include "filesys/cache.h"
#endif

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/**
 * Initializes the file system module. 
 * If FORMAT is true, reformats the file system. 
 */
void filesys_init(bool format) 
{
    fs_device = block_get_role(BLOCK_FILESYS);

    if (fs_device == NULL)
    {
        PANIC("No file system device found, can't initialize file system.");
    }

    inode_init();
    free_map_init();

    if (format) 
    {
        do_format();
    }

    free_map_open();

    dir_init();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done (void) 
{
#ifdef FILESYS_USE_CACHE
    cache_close();
#endif
    free_map_close ();
}
#ifdef FILESYS_SUBDIRS
static bool create_inode(block_sector_t sector, size_t size, block_sector_t parent, bool is_dir) {
    if (is_dir) {
        return dir_create(sector, size, parent);
    } else {
        return inode_create(sector, size, parent);
    }
}
#endif

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char *name, off_t initial_size, bool is_dir) 
{
#ifdef FILESYS_SUBIDRS
    // printf("filesys_create %s.\n", name);
    if (strlen(name) == 0) {
        return false;
    }
#endif

    block_sector_t inode_sector = 0;
    struct dir *parent_dir = NULL;
    char last_entry[NAME_MAX + 1];

#ifdef FILESYS_SUBDIRS
    char *path_prefix = (char*)malloc(strlen(name));

    // 1. Split the path into prefix_path + entry_name
    path_split_last(name, path_prefix, last_entry, NAME_MAX);

    // printf("Path was split between %s and %s.\n", path_prefix, last_entry);

    if (last_entry[0] == '\0') {
        return false;
    } else {
        // 2. Ensure prefix_path exists
        bool path_is_dir;
        struct inode *parent_inode = dir_open_from_path(path_prefix, &path_is_dir);
        // printf("inode: %d\n", inode_get_inumber(parent_inode));
        parent_dir = dir_open(parent_inode);
        if (parent_dir == NULL || !path_is_dir) {
            return false;
        }
    }
#endif

    // 3. Create a file with name entry_name in prefix_path
    bool success = (free_map_allocate (1, &inode_sector)
                    // && printf("Allocated sector: %d\n", inode_sector)
                    #ifdef FILESYS_SUBDIRS
                    && create_inode (inode_sector, initial_size, inode_get_inumber(dir_get_inode(parent_dir)), is_dir)
                    #else
                    && inode_create (inode_sector, initial_size)
                    #endif
                    && dir_add(parent_dir, last_entry, inode_sector, is_dir));

    if (!success && inode_sector != 0) 
    {
        free_map_release (inode_sector, 1);
    }

    dir_close (parent_dir);
    return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *filesys_open_file(const char *name)
{
    struct inode *inode = NULL;
#ifdef FILESYS_SUBDIRS
    bool is_dir;
    inode = dir_open_from_path(name, &is_dir);
    if (is_dir) {
        return NULL;
    } else {
        return file_open(inode);
    }
#else
    struct dir *dir = dir_open_root();
    if (dir != NULL)
        dir_lookup(dir, name, &inode);
    dir_close(dir);
    return file_open(inode);
#endif
}

#ifdef FILESYS_SUBDIRS
struct dir *filesys_open_dir(const char *path) {
    // printf("> filesys_open_dir\n");
    struct inode *inode = NULL;
    bool is_dir;
    inode = dir_open_from_path(path, &is_dir);
    if (is_dir) {
        // printf("< filesys_open_dir %d %d", is_dir, inode_get_inumber(inode));
        return dir_open(inode);
    } else {
        return NULL;
    }
}

bool filesys_path_exists(const char *path) {
    // printf("> filesys_path_exists %s\n", path);
    bool is_dir;
    bool exists = dir_open_from_path(path, &is_dir) != NULL;
    // printf("< filesys_path_exists %d\n", exists);
    return exists;
}

bool filesys_path_is_file(const char *path) {
    // printf("> filesys_path_is_file\n");
    bool is_dir;
    dir_open_from_path(path, &is_dir);
    // printf("< filesys_path_is_file: %d\n", !is_dir);
    return !is_dir;
}
#endif

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char *name) 
{
// #ifdef FILESYS_SUBDIRS
//     if (strlen(name) == 0) {
//         return false;
//     }

//     block_sector_t inode_sector = 0;

//     char *path_prefix = (char*)malloc(strlen(name));
//     char last_entry[NAME_MAX + 1];
//     struct dir *parent_dir = NULL;

//     // 1. Split the path into prefix_path + entry_name
//     path_split_last(name, path_prefix, last_entry, NAME_MAX);

//     // printf("Path was split between %s and %s.\n", path_prefix, last_entry);

//     if (last_entry[0] == '\0') {
//         return false;
//     } else {
//         // 2. Ensure prefix_path exists
//         bool path_is_dir;
//         struct inode *parent_inode = dir_open_from_path(path_prefix, &path_is_dir);
//         // printf("inode: %d\n", inode_get_inumber(parent_inode));
//         parent_dir = dir_open(parent_inode);
//         if (parent_dir == NULL || !path_is_dir) {
//             return false;
//         }

//         printf("removing %s from dir %s", name, parent_dir);
//         return dir_remove(parent_dir, name);
//     }
// #else
    struct dir *dir = dir_open_root ();
    bool success = dir != NULL && dir_remove(dir, name);
    dir_close (dir); 
    return success;
// #endif
}

/* Formats the file system. */
static void do_format (void)
{
    free_map_create ();
    if (!dir_create (ROOT_DIR_SECTOR, 16, true)) {
        PANIC ("root directory creation failed");
    }
    free_map_close ();
}

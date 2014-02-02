#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define LAST_SECTOR 0x00

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    block_sector_t next_sector;         /* Address of the next inode_disk sector */
    uint32_t file_total_size;           /* total size of the file */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[123];               /* Not used. */
};

static block_sector_t get_sector( struct inode_disk* , int );
static off_t get_size ( struct inode_disk* );
static block_sector_t get_last_sector( struct inode_disk* );
static struct inode_disk* get_last_inode_disk( struct inode_disk* );
static size_t get_number_of_sectors( struct inode_disk* );
static bool extend_inode( struct inode*, off_t );
static bool try_allocate( struct inode_disk*, size_t );

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
    struct inode_disk data;             /* Inode content. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length) // length holds the total size of the file
    return get_sector( &inode->data, pos / BLOCK_SECTOR_SIZE );
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
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
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
          struct inode_disk disk_inode = inode->data;
          //release for every inode_disk
          while ( disk_inode.next_sector != LAST_SECTOR )
          {
            free_map_release (disk_inode.start,
                            bytes_to_sectors (disk_inode.length));
            block_read( fs_device, disk_inode.next_sector, &disk_inode );
          }
          //release for the last inode_disk
          free_map_release( disk_inode.start, bytes_to_sectors( disk_inode.length ) );
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

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

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
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      // If the inode doesn't contains the sector
      if ( sector_idx == -1 )
      {
          if ( extend_inode( inode, offset + size ) )
          {
            return 0;
          }
          inode->data.file_total_size += ( offset + size );
          sector_idx = bytes_to_sectors( offset );
          sector_ofs = offset % BLOCK_SECTOR_SIZE;
      }

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
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

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
  return inode->data.file_total_size;
}

/* Returns the n sector */
static block_sector_t 
get_sector( struct inode_disk* disk_inode, int n )
{
  struct inode_disk* aux = disk_inode;

  while ( aux->length / BLOCK_SECTOR_SIZE < n )
  {
    n -= aux->length / BLOCK_SECTOR_SIZE;
    if ( aux->next_sector != LAST_SECTOR )
    {
      block_read( fs_device, aux->next_sector, aux );  
    }
    else // In case it tries to get sector which is greater
    {    // than the number of sectors in the file
      return LAST_SECTOR;
    }
  }

  return aux->start + n;
}

/* Returns the size of the file */
static off_t 
get_size ( struct inode_disk* disk_inode )
{
  off_t size = 0;
  struct inode_disk* aux = disk_inode;
  size += aux->length;

  while ( aux->next_sector != LAST_SECTOR )
  {
      block_read( fs_device, aux->next_sector, aux );
      size += aux->length;
  }

  return size;
}

/* Get last sector */
static block_sector_t
get_last_sector( struct inode_disk* disk_inode )
{
  return get_sector( disk_inode, disk_inode->file_total_size );
}

/* Get last inode_disk */
static struct inode_disk*
get_last_inode_disk( struct inode_disk* disk_inode )
{
  struct inode_disk* aux = disk_inode;
  while ( aux->next_sector != LAST_SECTOR )
  {
    block_read( fs_device, aux->next_sector, aux );
  }
  return aux;
}

/* Get number of sectors in file */
static size_t
get_number_of_sectors( struct inode_disk* disk_inode )
{
  return disk_inode->file_total_size / BLOCK_SECTOR_SIZE;
}

static bool
extend_inode( struct inode* inode, off_t offset )
{
  off_t gap = offset - inode->data.file_total_size;
  struct inode_disk* last_disk_inode = get_last_inode_disk( &inode->data );
  if ( !free_map_allocate( 1, &last_disk_inode->next_sector ) )
  {
    return false;
  }
  inode_create( last_disk_inode->next_sector, gap );
  return true;
}


static bool 
try_allocate( struct inode_disk* disk_inode, size_t blocks_number )
{
  struct inode_disk* disk_aux = disk_inode;
  size_t aux = blocks_number;
  size_t contor = 0;

  while ( aux != 0 )
  {
    if ( free_map_allocate( aux, &disk_aux->start ) )
    {
      contor+= aux;
      aux = blocks_number - aux;
      if ( contor == blocks_number )
      {
        return true;
      }
      else
      {
        struct inode_disk *new_disk_inode = NULL;
        new_disk_inode = calloc (1, sizeof *new_disk_inode);
        new_disk_inode->length = 0;
        new_disk_inode->magic = INODE_MAGIC;
        free_map_allocate( 1, &disk_aux->next_sector );
        block_write (fs_device, disk_aux->next_sector, new_disk_inode);
        free( new_disk_inode );
        block_read( fs_device,  disk_aux->next_sector, disk_aux );
      }
    }
    else
    {
      aux--;
    }
  }
  if ( contor == blocks_number )
  {
    return true;
  }
  return false;
}
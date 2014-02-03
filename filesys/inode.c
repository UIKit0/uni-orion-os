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
#define NULL_SECTOR 0x00
#define INODE_DISK_ARRAY_SIZE 62

//#define FILESYS_USE_CACHE
//#undef FILESYS

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
#ifdef FILESYS_EXTEND_FILES
    block_sector_t start[ INODE_DISK_ARRAY_SIZE ];           /* First data sector. */
    off_t length[ INODE_DISK_ARRAY_SIZE ];                   /* File size in bytes. */
    off_t file_total_size;              /* total size of the file */
    block_sector_t next_sector;         /* Address of the next inode_disk */
    unsigned magic;                     /* Magic number. */
    uint32_t unused;                    /* Not used */
#else
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
#endif
};


#ifdef FILESYS_EXTEND_FILES

static block_sector_t get_sector( const struct inode_disk* , int );
static off_t get_size ( struct inode_disk* );
static struct inode_disk* get_last_inode_disk( struct inode_disk* );
static bool extend_inode( struct inode*, off_t );
static bool try_allocate( struct inode_disk*, size_t );

#endif

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
  
#ifdef FILESYS_EXTEND_FILES
  if (pos < inode->data.file_total_size) // length holds the total size of the file
    return get_sector( &inode->data, pos / BLOCK_SECTOR_SIZE );
#else
  if (pos < inode->data.length) // length holds the total size of the file
  	return inode->data.start + pos / BLOCK_SECTOR_SIZE;
#endif
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
#ifdef FILESYS_EXTEND_FILES
      disk_inode->length[0] = length; //
#else
      disk_inode->length = length;
#endif
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
#ifdef FILESYS_EXTEND_FILES
          struct inode_disk disk_inode = inode->data;
          //Release for every inode_disk
          do
          {
            size_t contor = 0;
            while ( disk_inode.start[contor] != NULL_SECTOR )
            {
               //First it release every data sector from inode
               free_map_release ( disk_inode.start[contor], bytes_to_sectors ( disk_inode.length[contor] ) );
               contor++;
               if ( contor == INODE_DISK_ARRAY_SIZE )
               {
                //If contor reach the size of the array
                //Then read next_sector
                block_read( fs_device, disk_inode.next_sector, &disk_inode );
               }
            }
          }while ( disk_inode.next_sector != NULL_SECTOR );
#else
          free_map_release (inode->data.start, bytes_to_sectors (inode->data.length));
#endif
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
#ifndef FILESYS_USE_CACHE
  uint8_t *bounce = NULL;
#endif

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
#ifndef FILESYS_USE_CACHE
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
#else
          cache_read(sector_idx, buffer, sector_ofs, size);
#endif
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
#ifndef FILESYS_USE_CACHE
  free (bounce);
#endif

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
#ifndef FILESYS_USE_CACHE
  uint8_t *bounce = NULL;
#endif

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

#ifdef FILESYS_EXTEND_FILES
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
#endif

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
#ifndef FILESYS_USE_CACHE
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
#else
          cache_write(sector_idx, buffer, sector_ofs, size);
#endif
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
#ifndef FILESYS_USE_CACHE
  free (bounce);
#endif

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
#ifdef FILESYS_EXTEND_FILES
	return inode->data.file_total_size;
#else
	return inode->data.length;
#endif
}

#ifdef FILESYS_EXTEND_FILES
/* Returns the n sector */
static block_sector_t 
get_sector( const struct inode_disk* disk_inode, int n )
{
 struct inode_disk* aux = disk_inode;
 int contor = 0;

 while ( aux->length[contor] / BLOCK_SECTOR_SIZE < n )
 {
    //In case it reach a sector that don't store any address
    //Then n is greater then the hole file
    if ( aux->start[contor] != NULL_SECTOR )
    {
      return NULL_SECTOR;
    }

    if ( contor < INODE_DISK_ARRAY_SIZE )
    {
      //If doesn't reach the last data sector from inode_disk
      n -= aux->length[contor] / BLOCK_SECTOR_SIZE;
      contor++;
    }
    else
    {
      //In case it pass over all data sector, it starts to read from next inode_disk
      contor = 0;
#ifndef FILESYS_USE_CACHE
      block_read( fs_device, aux->next_sector, aux );
#else
      cache_read( aux->next_sector, aux, 0, BLOCK_SECTOR_SIZE );
#endif
    }
 }

 // In case it doesn't contains any data
 if ( aux->start[contor] != NULL_SECTOR )
 {
    return aux->start[contor] + n;
 }
 else
 {
    return NULL_SECTOR;
 }
}

/* Returns the size of the file */
static off_t 
get_size ( struct inode_disk* disk_inode )
{
  off_t size = 0;
  int contor = 0;

  struct inode_disk* aux = disk_inode;

  while( aux->start[contor] != NULL_SECTOR )
  {
    //Tests if it reads all data sectors from inode_disk
    if ( contor < INODE_DISK_ARRAY_SIZE )
    {
      size += aux->length[contor];
      contor++;
    }
    else
    {
      //If it reads all data sectors, reads next_sector.
      contor = 0;
#ifndef FILESYS_USE_CACHE
      block_read( fs_device, aux->next_sector, aux );  
#else
      cache_read( aux->next_sector, aux, 0, BLOCK_SECTOR_SIZE );
#endif
    }
  }
  return size;
}

/* Get last inode_disk */
static struct inode_disk*
get_last_inode_disk( struct inode_disk* disk_inode )
{
  struct inode_disk* aux = disk_inode;
  while ( aux->next_sector != NULL_SECTOR )
  {
#ifndef FILESYS_USE_CACHE
    block_read( fs_device, aux->next_sector, aux );
#else
    cache_read( aux->next_sector, aux, 0, BLOCK_SECTOR_SIZE );
#endif
  }
  return aux;
}

static bool
extend_inode( struct inode* inode, off_t offset )
{
  //Calculates the size of the gap.
  off_t gap = offset - inode->data.file_total_size;
  //Get the last of disk_inode because this disk_inode should 
  //store the new sectors that would be added.
  struct inode_disk* last_disk_inode = get_last_inode_disk( &inode->data );
  //Add the new sectors, in case the disk_inode consume all
  //the sectors, try_allocate also creates a new disk_inode and
  //stores the remain data in it and connect the sectors.
  try_allocate( last_disk_inode, gap / BLOCK_SECTOR_SIZE );
}


static bool 
try_allocate( struct inode_disk* disk_inode, size_t blocks_number )
{
  struct inode_disk* disk_aux = disk_inode;
  size_t contor = 0;

  //Finds first sector data that is free
  while( disk_aux->start[contor] != NULL_SECTOR )
  {
    contor++;
  }

  size_t numberOfBlocksToAllocate = blocks_number;
  size_t numberOfBlocksAllocated = 0;
  while( numberOfBlocksAllocated != blocks_number )
  {
    if ( free_map_allocate( numberOfBlocksToAllocate, &disk_aux->start[contor] ) )
    {

      numberOfBlocksAllocated += numberOfBlocksToAllocate;
      if ( numberOfBlocksAllocated != blocks_number )
      {
        //It doesn't allocates all the blocks yet
        contor++;

        if ( contor == INODE_DISK_ARRAY_SIZE )
        {
          //In case it reach the maximum number of data sectors
          //We have to create another inode_disk, and continue to add data in it.
          struct inode_disk* new_inode_disk = NULL;
          new_inode_disk = calloc( 1, sizeof * new_inode_disk );
          new_inode_disk->magic = INODE_MAGIC;
          free_map_allocate( 1, &disk_aux->next_sector );
#ifndef FILESYS_USE_CACHE
          block_write( fs_device, disk_aux->next_sector, new_inode_disk );
#else
          cache_write( disk_aux->next_sector, new_inode_disk, 0, BLOCK_SECTOR_SIZE );
#endif

          free( new_inode_disk );

#ifndef FILESYS_USE_CACHE
          block_read( fs_device, disk_aux->next_sector, disk_aux );
#else
          cache_read( disk_aux->next_sector, disk_aux, 0, BLOCK_SECTOR_SIZE );
#endif
        }
      }
    }
    else
    {
      //In case it doesn't succed to allocate then 
      //reduce the number of consecutive blocks to allocate
      numberOfBlocksToAllocate--;
      if ( numberOfBlocksToAllocate == 0 )
      {
        return false;
      }
    }
  }

  if ( contor == blocks_number )
  {
    return true;
  }

  return false;
}
#endif

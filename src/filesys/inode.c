#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define D_BLOCK 122
#define I_BLOCK 128
#define DI_BLOCK 128 * 128

typedef block_sector_t Block[I_BLOCK];

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t d_blocks[D_BLOCK];
    block_sector_t i_blocks;
    block_sector_t di_blocks;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_dir;
    uint32_t parent;
  };

bool inode_alloc(struct inode_disk *,block_sector_t);
int inode_direct_alloc(struct inode_disk *, block_sector_t);
int inode_indirect_alloc(struct inode_disk *, block_sector_t);
int inode_doubly_indirect_alloc(struct inode_disk *, block_sector_t);

bool inode_delloc(struct inode_disk *, block_sector_t );
int inode_direct_delloc(struct inode_disk *, block_sector_t );
int inode_indirect_delloc(struct inode_disk *, block_sector_t );
int inode_doubly_indirect_delloc(struct inode_disk *, block_sector_t );

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
    int is_dir;
    struct inode_disk data;             /* Inode content. */
    struct inode* parent;
  };

static block_sector_t pos_to_index (struct inode_disk *disk, off_t pos) {
  off_t bound = D_BLOCK;
  off_t length = disk->length;
  int i;
  bound = bound < length ? bound : length;

  if (pos <bound){
    return disk->d_blocks[pos];
  }

  bound = bound + I_BLOCK;
  bound = bound < length ? bound : length;

  if(pos < bound){
    block_sector_t block[I_BLOCK];
    buffer_cache_read(disk->i_blocks, block);

    return block[pos - D_BLOCK] ;
  }

  off_t before = bound;
  
  bound = bound + DI_BLOCK;
  bound = bound < length ? bound : length;
  if(pos < bound){
    block_sector_t block[I_BLOCK];
    block_sector_t in_block[I_BLOCK][I_BLOCK];

    buffer_cache_read(disk->di_blocks, block);
    int t = (bound-before);
    int row = t / 128;

    for(i=0; i<row; i++){
      buffer_cache_read(block[i], in_block[i]);
    }
    t = pos - before;
    return in_block[t/128][t%128];
  }

  return -1;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length){
    return pos_to_index(&inode->data, pos);
  }
    // return pos / BLOCK_SECTOR_SIZE;
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
inode_create (block_sector_t sector, off_t length, int is_dir, void *parent)
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
      disk_inode->is_dir = is_dir;
      disk_inode->parent = (uint32_t)parent;
      if (inode_alloc(disk_inode, sectors)) 
        {
          block_write (fs_device, sector, disk_inode);
          // buffer_cache_write (sector, disk_inode);
          // if (sectors > 0) 
          //   {
          //     static char zeros[BLOCK_SECTOR_SIZE];
          //     size_t i;
              
          //     for (i = 0; i < sectors; i++) 
          //       block_write (fs_device, disk_inode->start + i, zeros);
          //       // buffer_cache_write (disk_inode->start + i, zeros);

          //   }
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
  inode->is_dir = inode->data.is_dir;
  inode->parent = (struct inode *)inode->data.parent;
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

  // printf("close inode %d\n", inode->sector);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
          inode_delloc(&inode->data, bytes_to_sectors(inode->data.length));
        }

      free (inode);
      inode = NULL; 
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
          buffer_cache_read(sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
            // printf("in else state // R2 %d, %d\n", sector_idx, inode->sector);

            if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
            buffer_cache_read(sector_idx, bounce);
            memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  // printf("size %d  byte %d\n", size, bytes_read);
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
          buffer_cache_write(sector_idx, buffer + bytes_written);
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
              buffer_cache_read (sector_idx, bounce);
            else
              memset (bounce, 0, BLOCK_SECTOR_SIZE);
            memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
            buffer_cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  printf("bytes written %d\n", bytes_written);
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


bool is_inode_dir(struct inode *inode){
  if(inode->is_dir != 0){
    return true;
  }else{
    return false;
  }
}

struct inode *inode_get_parent(struct inode *inode){
  return inode->parent;
}

bool is_inode_removed(struct inode *inode){
	return inode->removed;
}

bool inode_alloc(struct inode_disk *disk_inode, block_sector_t sectors){

  sectors -= inode_direct_alloc(disk_inode, sectors);
  if(sectors == 0){
    return true;
  }
  else if(sectors > 0){
    sectors -= inode_indirect_alloc(disk_inode, sectors);
    if(sectors == 0){
      return true;
    }else if(sectors > 0){
      sectors -= inode_doubly_indirect_alloc(disk_inode, sectors);
      if(sectors == 0){
        return true;
      }
    }
  }

  return false;
}

int inode_direct_alloc(struct inode_disk *disk_inode, block_sector_t sectors){
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t i, length;

  int blockSize = D_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  for(i=0; i<length; i++){
    free_map_allocate (1, &disk_inode->d_blocks[i]);
    buffer_cache_write (disk_inode->d_blocks[i], zeros);
  }

  return length;
}

int inode_indirect_alloc(struct inode_disk *disk_inode, block_sector_t sectors)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t i, length;

  int blockSize = I_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  block_sector_t *i_blocks = disk_inode->i_blocks;
  block_sector_t block[I_BLOCK];
  if(*i_blocks == 0){
    free_map_allocate(1, i_blocks);
    buffer_cache_write(*i_blocks, zeros);
  }
  buffer_cache_read(*i_blocks, &block);

  for(i=0; i<length; i++){
    if(block[i] == 0){    
      free_map_allocate (1, &block[i]);
      buffer_cache_write (block[i], zeros);
    }  
  } 

  buffer_cache_write(*i_blocks, block);
  return length;
  
}
int inode_doubly_indirect_alloc(struct inode_disk *disk_inode, block_sector_t sectors){
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t i, j, length;
  size_t row;

  int blockSize = DI_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  block_sector_t *di_blocks = disk_inode->di_blocks;
  block_sector_t block[I_BLOCK];
  block_sector_t in_block[I_BLOCK][I_BLOCK];
  if(*di_blocks == 0){
    free_map_allocate(1, di_blocks);
    buffer_cache_write(*di_blocks, zeros);
  }

  buffer_cache_read(*di_blocks, block);
  row = length / I_BLOCK;
  
  for(i=0; i<row; i++){
    if(block[i] == 0){
      free_map_allocate (1, &block[i]);
      buffer_cache_write (block[i], zeros);
    }
      buffer_cache_read(block[i], in_block[i]);
  }

  for(i=0; i<length; i++){
    if(block[i] == 0){
      free_map_allocate (1, &in_block[i/128][i%128]);
      buffer_cache_write (in_block[i/128][i%128], zeros);
    }
  }
  for(i=0; i<row; i++){
    buffer_cache_write(block[i], in_block[i]);
  }

  buffer_cache_write(di_blocks, &block);
  return length;
}


/////////////////////////////////////////////////////////////////////////////////////////
bool inode_delloc(struct inode_disk *disk_inode, block_sector_t sectors){

  sectors -= inode_direct_delloc(disk_inode, sectors);
  if(sectors == 0){
    return true;
  }
  else if(sectors > 0){
    sectors -= inode_indirect_delloc(disk_inode, sectors);
    if(sectors == 0){
      return true;
    }else if(sectors > 0){
      sectors -= inode_doubly_indirect_delloc(disk_inode, sectors);
      if(sectors == 0){
        return true;
      }
    }
  }

  return false;
}


int inode_direct_delloc(struct inode_disk *disk_inode, block_sector_t sectors){
  size_t i, length;

  int blockSize = D_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  for(i=0; i<length; i++){
    free_map_release (disk_inode->d_blocks[i], 1);
  }

  return length;
}

int inode_indirect_delloc(struct inode_disk *disk_inode, block_sector_t sectors)
{
  size_t i, length;

  int blockSize = I_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  block_sector_t *i_blocks = disk_inode->i_blocks;
  block_sector_t block[I_BLOCK];

  buffer_cache_read(*i_blocks, &block);

  for(i=0; i<length; i++){
      free_map_release (block[i], 1);
  } 

  free_map_release(*i_blocks, 1);
  return length;
  
}
int inode_doubly_indirect_delloc(struct inode_disk *disk_inode, block_sector_t sectors){
  size_t i, j, length;
  size_t row;

  int blockSize = DI_BLOCK;
  length = sectors < blockSize ? sectors : blockSize;

  block_sector_t *di_blocks = disk_inode->di_blocks;
  block_sector_t block[I_BLOCK];
  block_sector_t in_block[I_BLOCK][I_BLOCK];

  buffer_cache_read(*di_blocks, block);
  row = length / I_BLOCK;
  
  for(i=0; i<row; i++){
      buffer_cache_read(block[i], in_block[i]);
  }

  for(i=0; i<length; i++){
      free_map_release (in_block[i/128][i%128], 1);
  }
  for(i=0; i<row; i++){
    free_map_release(block[i], 1);
  }

  free_map_release(*di_blocks, 1);
  return length;
}
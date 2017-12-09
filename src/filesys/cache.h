#include "device/block.h"
#include "thread/thread.h"

#define MAX_BUFFER_CACHE_NUM 64

struct buffer_cache {
  int cache_id; 
  struct inode *inode;
  block_sector_t sector_id;

  int sector_ofs;
  int chunk_size;

  void *cache;
  bool is_dirty;
  struct list_elem elem;
};

/* need to init in filesys_init function */
static struct list buffer_cache_list;
static int buffer_cache_num;

struct buffer_cache *create_buffer_cache (struct inode *inode, block_sector_t sector_id, void *data, int sector_ofs, int chunk_size);
struct buffer_cache *get_buffer_cache (struct inode *inode, block_sector_t sector_id);





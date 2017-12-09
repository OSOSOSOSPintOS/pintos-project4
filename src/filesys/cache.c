#include "filesys/cache.h"

struct buffer_cache *create_buffer_cache (struct inode *inode, block_sector_t sector_id, void *data, int sector_ofs, int chunk_size){
  // struct thread *t = thread_current();
  int id;

  // need to execute function that check buffer_cahce_list is max size & evict cache from list and return that id

  // if buffer_cahce_list is not max
  buffer_cache_num++;
  id = buffer_cache_num;

  struct buffer_cache *bce = NULL; 
  bce = (struct buffer_cache *)malloc(sizeof(struct buffer_cache));
  if(!bce){
    buffer_cache_num--;
    return bce;
  }
  bce->inode = inode;
  bce->sector_id = sector_id;
  bce->cache_id = id;
  bce->cache = malloc(BLOCK_SECTOR_SIZE);

  memcpy(bce->cache + sector_ofs, data + sector_ofs, chunk_size);
  bce->sector_ofs = sector_ofs;
  bce->chunk_size = chunk_size;

  bce->is_dirty = false;

  return bce;

}


// if buffer_cahce_list has buffer_cache which inode and sector_id is same with INODE and SECTOR_id  
// then return that buffer_cache
// else return NULL
struct buffer_cache *get_buffer_cache (struct inode *inode, block_sector_t sector_id){
  struct buffer_cache *bce = NULL;

  struct list_elem *e = list_begin(&buffer_cache_list);
	for(e = list_begin(&buffer_cache_list); e!=list_end(&buffer_cache_list); e=list_next(e))
	{
		bce = list_entry(e, struct buffer_cache, elem);
		if(bce->inode = inode && bce->sector_id == sector_id){
      return bce;
    }
	}
  return NULL;
}


#include "filesys/cache.h"


struct buffer_cache *buffer_cache_list[64];
int buffer_cache_num;
int old_one;
bool check_init;

void buffer_cache_init(){
  check_init = false;
  int i;
  for (i=0; i<64; i++){
    buffer_cache_list[i] = NULL;
  }
  buffer_cache_num = 0;
  old_one = 0;
  check_init = true;
}

struct buffer_cache *create_buffer_cache (struct inode *inode, block_sector_t sector_id, void *data, int sector_ofs, int chunk_size){

  // need to execute function that check buffer_cahce_list is max size & evict cache from list and return that id

  // if buffer_cahce_list is not max
  // buffer_cache_num++;

  struct buffer_cache *bce = NULL; 
  bce = (struct buffer_cache *)malloc(sizeof(struct buffer_cache));
  if(!bce){
    return bce;
  }
  bce->inode = inode;
  bce->sector_id = sector_id;
  
  bce->cache = malloc(BLOCK_SECTOR_SIZE);

  memcpy(bce->cache, data, chunk_size);
  bce->sector_ofs = sector_ofs;
  bce->chunk_size = chunk_size;

  bce->is_dirty = false;

  return bce;

}

void push_buffer_cache_to_list (struct buffer_cache *cache){
  struct buffer_cache *front = NULL;
  struct list_elem *e;
  if(buffer_cache_num >= 64){
    front = buffer_cache_list[old_one];
    buffer_cache_list[old_one] = NULL;
    free(front->cache);
    free(front);
    buffer_cache_list[old_one] = cache;
    old_one ++;
    if(old_one > 64){
      old_one = 0;
    }
  }else{
    if(!check_init){
      free(cache->cache);
      free(cache);
      return;
    }
    buffer_cache_list[buffer_cache_num] = cache;
    buffer_cache_num++;
  }
  
}



// if buffer_cahce_list has buffer_cache which inode and sector_id is same with INODE and SECTOR_id  
// then return that buffer_cache
// else return NULL
struct buffer_cache *get_buffer_cache (struct inode *inode, block_sector_t sector_id){
  int i;
  struct buffer_cache *bce = NULL;

  if(!check_init){
    return NULL;
  }
  struct list_elem *e = list_begin(&buffer_cache_list);
	for(i = 0; i<buffer_cache_num; i++)
	{
		bce = buffer_cache_list[i];
    if(bce == NULL){
      continue;
    }
		if(bce->inode == inode && bce->sector_id == sector_id){
      return bce;
    }
	}
  return NULL;
}


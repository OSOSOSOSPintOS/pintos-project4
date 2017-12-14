#include "filesys/cache.h"
#include "filesys/filesys.h"


struct buffer_cache *buffer_cache_list[64];
int buffer_cache_num;
int old_one;
bool check_init;

void buffer_cache_init(){
  check_init = false;
  
  int i;
  lock_init(&buffer_cache_lock);
  for (i=0; i<64; i++){
    buffer_cache_list[i] = NULL;
  }
  buffer_cache_num = 0;
  old_one = 0;
  // printf("initiating buff_cache\n");
  check_init = true;
}

struct buffer_cache *create_buffer_cache (block_sector_t sector_id, void* data){

  // need to execute function that check buffer_cahce_list is max size & evict cache from list and return that id


  struct buffer_cache *bce = NULL; 
  bce = (struct buffer_cache *)malloc(sizeof(struct buffer_cache));
  if(!bce){
    return bce;
  }
  bce->sector_id = sector_id;
  bce->cache = malloc(BLOCK_SECTOR_SIZE);

  memcpy(bce->cache , data , BLOCK_SECTOR_SIZE);
  bce->is_dirty = false;

  return bce;

}

void push_buffer_cache_to_list (struct buffer_cache *cache){
  struct buffer_cache *front = NULL;
  struct list_elem *e;
  // lock_acquire(&buffer_cache_lock);
  if(buffer_cache_num >= 64){
    if(buffer_cache_list[old_one]->is_dirty){
      block_write (fs_device, buffer_cache_list[old_one]->sector_id, buffer_cache_list[old_one]->cache);
      buffer_cache_list[old_one]->is_dirty = false;
    }
    free(buffer_cache_list[old_one]->cache);
    free(buffer_cache_list[old_one]);
    buffer_cache_list[old_one] = cache;
    old_one ++;
    if(old_one >= 64){
      old_one = 0;
    }
  }else{
    if(!check_init){
      free(cache->cache);
      free(cache);
      // lock_release(&buffer_cache_lock);
      return;
    }
    buffer_cache_list[buffer_cache_num] = cache;
    buffer_cache_num++;
    // printf("push new cache %d\n", buffer_cache_num);
  }
  // lock_release(&buffer_cache_lock);
}

void clear_buffer_cache_list(){
  int i;
  // lock_acquire(&buffer_cache_lock);
  for(i=0; i<64; i++){
    if(buffer_cache_list[i] != NULL){
      if(buffer_cache_list[i]->is_dirty){
        block_write (fs_device, buffer_cache_list[i]->sector_id, buffer_cache_list[i]->cache);
        buffer_cache_list[i]->is_dirty = false;
      }
      free(buffer_cache_list[i]->cache);
      free(buffer_cache_list[i]);
      buffer_cache_list[i] = NULL;
    }
  }
  // lock_release(&buffer_cache_lock);
}


// if buffer_cahce_list has buffer_cache which inode and sector_id is same with INODE and SECTOR_id  
// then return that buffer_cache
// else return NULL
struct buffer_cache *get_buffer_cache (block_sector_t sector_id){
  int i;
  struct buffer_cache *bce = NULL;

  // lock_acquire(&buffer_cache_lock);


  if(!check_init){
    // lock_release(&buffer_cache_lock);
    return NULL;
  }
  struct list_elem *e = list_begin(&buffer_cache_list);
	for(i = 0; i<buffer_cache_num; i++)
	{
		bce = buffer_cache_list[i];
    if(bce == NULL){
      continue;
    }
		if(bce->sector_id == sector_id){
      // lock_release(&buffer_cache_lock);
      return bce;
    }
	}
  // lock_release(&buffer_cache_lock);
  return NULL;
}


void buffer_cache_read(block_sector_t sector_id, void *buffer){
  lock_acquire(&buffer_cache_lock);

  struct buffer_cache *bce = NULL;
  int i;
  bce = get_buffer_cache(sector_id);
  if(bce){
    memcpy(buffer, bce->cache, BLOCK_SECTOR_SIZE);
  }
  else{
    block_read (fs_device, sector_id, buffer);
    bce = create_buffer_cache(sector_id, buffer);
    push_buffer_cache_to_list(bce);
  }

  lock_release(&buffer_cache_lock);
}

void buffer_cache_write(block_sector_t sector_id, void *buffer){
  lock_acquire(&buffer_cache_lock);

  struct buffer_cache *bce = NULL;
  int i;
  bce = get_buffer_cache(sector_id);
  if(bce){
    memcpy(bce->cache, buffer, BLOCK_SECTOR_SIZE);
  }
  else{
    // void *temp = malloc(BLOCK_SECTOR_SIZE);
    // block_read (fs_device, sector_idx, temp);
    bce = create_buffer_cache(sector_id, buffer);
    push_buffer_cache_to_list(bce);
    // memcpy(cache->cache, buffer, chunk_size);
  }

  bce->is_dirty = true;

  lock_release(&buffer_cache_lock);
}

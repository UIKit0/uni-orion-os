#include "cache.h"
#include <threads/synch.h>
#include <threads/thread.h>
#include <threads/malloc.h>
#include <devices/timer.h>
#include <lib/string.h>
#include "filesys.h"
#include "free-map.h"
/**
	compilation options
*/
#define CACHE_SIZE_IN_SECTORS 64
#define SECTOR_SIZE_IN_BYTES 512
#define DUMP_INTERVAL_TICKS 10

/**
	data structures
*/
struct sector_t {
	char data[SECTOR_SIZE_IN_BYTES];
};
typedef struct sector_t sector_t;

struct sector_supl_t {
	bool present;
	bool accessed;
	bool dirty;
	int pinned; //pin counter
	struct lock *s_lock;
	int sector_index_in_cache;
	sid_t sector_index; 
};
typedef struct sector_supl_t sector_supl_t;


struct buffer_cache {
	sector_t cache[CACHE_SIZE_IN_SECTORS];
	sector_supl_t cache_aux[CACHE_SIZE_IN_SECTORS];
	struct lock ss_lock;
};
typedef struct buffer_cache buffer_cache;


/**
	internal function declarations
*/


//can evict cache sectors	
//if there is no eviction will just supply the correct index 
//and lock the page such that a concurrent eviction will not evict the same page
int cache_atomic_get_supl_data_and_pin(sector_supl_t *data, sid_t index);


//will write back the data and will enable eviction for this cache entry
void cache_atomic_set_supl_data_and_unpin(int cache_sector_index, sector_supl_t *data);

int cache_evict(void);
int cache_lru(void);
void cache_read_ahead_asynch(sid_t index);
void cache_read_ahead_internal(sid_t index);

void cache_dump_all(void);
void cache_dump_entry(int entry_index);

void cache_read_internal(int cache_sector_index, sid_t sector_index);


/**
	main cache thread
	- dumps the cache periodically
	- processes the read_ahead requests
*/
void cache_main(void *aux UNUSED);




/**
	the cache data
*/
buffer_cache gCache;
bool gIsCacheThreadRunning;
int gLruCursor;

void cache_write(sid_t index, void *buffer, int offset, int size) {	
	sector_supl_t info;
	int sdataIndex = cache_atomic_get_supl_data_and_pin(&info, index);
	
	if(info.present) {
		//printf("cache_write present %d %d\n", index, sdataIndex);
		info.dirty = true;
		lock_acquire(info.s_lock);
		memcpy(gCache.cache[info.sector_index_in_cache].data + offset, buffer, size);
		lock_release(info.s_lock);
	}
	else {
		//printf("cache_write not present %d %d\n", index, sdataIndex);
		cache_read_internal(sdataIndex, index);
		info.present = true;		
		info.accessed = false;
		info.sector_index_in_cache = sdataIndex;
		info.sector_index = index;

		info.dirty = true;
		lock_acquire(info.s_lock);
		memcpy(gCache.cache[info.sector_index_in_cache].data + offset, buffer, size);
		lock_release(info.s_lock);		
	}
	cache_atomic_set_supl_data_and_unpin(sdataIndex, &info);
}

void cache_read(sid_t index, void *buffer, int offset, int size) {
	sector_supl_t info;
	int sdataIndex = cache_atomic_get_supl_data_and_pin(&info, index);
	
	if(info.present) {
		//printf("cache_read present %d %d\n", index, sdataIndex);
		info.accessed = true;
		lock_acquire(info.s_lock);
		memcpy(buffer, gCache.cache[info.sector_index_in_cache].data + offset, size);
		lock_release(info.s_lock);
	}
	else {
		//printf("cache_read not present %d %d\n", index, sdataIndex);
		cache_read_internal(sdataIndex, index);
		info.present = true;		
		info.accessed = true;
		info.sector_index_in_cache = sdataIndex;
		info.sector_index = index;

		info.dirty = false;
		lock_acquire(info.s_lock);
		memcpy(buffer, gCache.cache[info.sector_index_in_cache].data + offset, size);
		lock_release(info.s_lock);
		cache_read_ahead_asynch(index + 1);
	}
	cache_atomic_set_supl_data_and_unpin(sdataIndex, &info);
	
}

void cache_init(void) {
	lock_init(&gCache.ss_lock);
	int i;

	for(i = 0; i < CACHE_SIZE_IN_SECTORS; ++i) {
		memset(&gCache.cache_aux[i], 0, sizeof(sector_supl_t));

		gCache.cache_aux[i].s_lock = (struct lock *)malloc(sizeof(struct lock));

		lock_init(gCache.cache_aux[i].s_lock);
		gCache.cache_aux[i].present = false;
		gCache.cache_aux[i].pinned = 0;
		gCache.cache_aux[i].accessed = false;
		gCache.cache_aux[i].dirty = false;
	}
	gIsCacheThreadRunning = true;
	gLruCursor = 0;
	//printf("cache: Initialized cache with %d sectors\n", CACHE_SIZE_IN_SECTORS);
	thread_create ("cache_thread", 0, cache_main, NULL);
}

void cache_close(void) {	
	cache_dump_all();
	gIsCacheThreadRunning = false;
	int i;

	lock_acquire(&gCache.ss_lock);
	for(i = 0; i < CACHE_SIZE_IN_SECTORS; ++i) {		
		free(gCache.cache_aux[i].s_lock);
		memset(&gCache.cache_aux[i], 0, sizeof(sector_supl_t));
		
		gCache.cache_aux[i].present = false;
		gCache.cache_aux[i].dirty = false;
		gCache.cache_aux[i].s_lock = NULL;
	}
	lock_release(&gCache.ss_lock);
}

void cache_dump_all(void) {
	lock_acquire(&gCache.ss_lock);
	int i = 0;
	for(i = 0; i < CACHE_SIZE_IN_SECTORS; ++i) {
		cache_dump_entry(i);
	}
	lock_release(&gCache.ss_lock);
}

void cache_dump_entry(int index) {
	if(gCache.cache_aux[index].dirty) {

		ASSERT(gCache.cache_aux[index].present);
		block_write( fs_device, gCache.cache_aux[index].sector_index, gCache.cache[index].data );
	}
}

int advance(int);
int advance(int glru) {
	return (glru + 1) % CACHE_SIZE_IN_SECTORS;
}

int retreat(int);
int retreat(int glru) {
	return (glru + CACHE_SIZE_IN_SECTORS - 1) % CACHE_SIZE_IN_SECTORS;
}

int cache_lru(void) {
	int it;

	for(it = 0; it != CACHE_SIZE_IN_SECTORS; ++it) {
		if(!gCache.cache_aux[it].present)
			return it;
	}

	for(it = gLruCursor; ; it = advance(it)) {
		if(!gCache.cache_aux[it].pinned &&
			!gCache.cache_aux[it].accessed &&
			gCache.cache_aux[it].present) {
			return it;
		}
		else {
			gCache.cache_aux[it].accessed = false;
		}
	}
	ASSERT(!"no more free cache pages");
}

void cache_read_ahead_internal(sid_t index) {

}

void cache_read_ahead_asynch(sid_t index) {

}

void cache_main(void *aux UNUSED) {
	while(gIsCacheThreadRunning) {
		cache_dump_all();		
		timer_sleep(DUMP_INTERVAL_TICKS);
	}
}

int cache_evict(void) {
	int ev_id = cache_lru();

	//printf("cache_evict %d\n", ev_id);
	cache_dump_entry(ev_id);
	gCache.cache_aux[ev_id].present = false;
	ASSERT(gCache.cache_aux[ev_id].pinned == 0);
	return ev_id;
}

int cache_atomic_get_supl_data_and_pin(sector_supl_t *data, sid_t index) {
	lock_acquire(&gCache.ss_lock);
	int i = 0;
	int found_index = -1;
	for(i = 0; i < CACHE_SIZE_IN_SECTORS; ++i) {
		if(gCache.cache_aux[i].sector_index == index) {
			found_index = i;
			break;
		}
	}
	

	if(found_index == -1) {
		found_index = cache_evict();
	}	
	gCache.cache_aux[found_index].pinned++;
	memcpy(data, &gCache.cache_aux[found_index], sizeof(sector_supl_t));
	lock_release(&gCache.ss_lock);	
	return found_index;
}


void cache_atomic_set_supl_data_and_unpin(int cache_sector_index, sector_supl_t *data) {
	lock_acquire(&gCache.ss_lock);
	//sanity checks
	ASSERT(cache_sector_index >= 0 && cache_sector_index < CACHE_SIZE_IN_SECTORS);
	ASSERT(gCache.cache_aux[cache_sector_index].pinned);
	memcpy(&gCache.cache_aux[cache_sector_index], data, sizeof(sector_supl_t));
	gCache.cache_aux[cache_sector_index].pinned--;
	lock_release(&gCache.ss_lock);
}

void cache_read_internal(int cache_sector_index, sid_t sector_index) {
	block_read( fs_device, sector_index, gCache.cache[cache_sector_index].data);
}

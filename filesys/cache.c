#include "cache.h"
#include <threads/synch.h>
#include <threads/thread.h>
#include <threads/malloc.h>
#include <devices/timer.h>
#include <lib/string.h>
/**
	compilation options
*/
#define CACHE_SIZE_IN_SECTORS 64
#define SECTOR_SIZE_IN_BYTES 512
#define DUMP_INTERVAL_TICKS 25

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

void cache_write(sid_t index, void *buffer, int offset, int size) {
	sector_supl_t info;
	int sdataIndex = cache_atomic_get_supl_data_and_pin(&info, index);
	
	if(info.present) {
		info.dirty = true;
		lock_acquire(info.s_lock);
		memcpy(gCache.cache[info.sector_index_in_cache].data + offset, buffer, size);
		lock_release(info.s_lock);
	}
	else {
		cache_read_internal(sdataIndex, index);
		info.present = true;		
		info.accessed = false;
		info.sector_index_in_cache = sdataIndex;

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
		info.accessed = true;
		lock_acquire(info.s_lock);
		memcpy(buffer, gCache.cache[info.sector_index_in_cache].data + offset, size);
		lock_release(info.s_lock);
	}
	else {
		cache_read_internal(sdataIndex, index);
		info.present = true;		
		info.accessed = true;
		info.sector_index_in_cache = sdataIndex;

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
	}
	gIsCacheThreadRunning = true;
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
	//writeback
}

int cache_lru(void) {
	return 0;
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
	lock_acquire(&gCache.ss_lock);
	int ev_id = cache_lru();
	cache_dump_entry(ev_id);
	gCache.cache_aux[ev_id].present = false;
	ASSERT(gCache.cache_aux[ev_id].pinned == 0);
	lock_release(&gCache.ss_lock);
	return ev_id;
}

int cache_atomic_get_supl_data_and_pin(sector_supl_t *data, sid_t index) {
	lock_acquire(&gCache.ss_lock);
	int i = 0;
	int found_index = -1;
	for(i = 0; i < CACHE_SIZE_IN_SECTORS; ++i) {
		if(gCache.cache_aux[i].sector_index == index) {
			memcpy(data, &gCache.cache_aux[i], sizeof(sector_supl_t));
			gCache.cache_aux[i].pinned++;
			found_index = i;
		}
	}
	lock_release(&gCache.ss_lock);

	if(found_index == -1) {
		found_index = cache_evict();
		memcpy(data, &gCache.cache_aux[found_index], sizeof(sector_supl_t));
		gCache.cache_aux[i].pinned++;
		found_index = i;
	}
	
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

}

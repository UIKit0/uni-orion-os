#include "cache.h"
#include <threads/synch.h>
/**
	compilation options
*/
#define CACHE_SIZE_IN_SECTORS 64
#define SECTOR_SIZE_IN_BYTES 512

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
	struct lock sector_lock;
	int sector_index_in_cache;
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

int cache_get_cache_sector_index_by_sector_index(sid_t index);
int cache_evict(int cache_sector_index);
int cache_lru(void);
int cache_read_ahead_asynch(sid_t index);
void cache_dump(void);


/**
	main cache thread
	- dumps the cache periodically
	- processes the read_ahead requests
*/
void cache_main(void);


/**
	the cache data
*/
buffer_cache gCache;

void cache_write(sid_t index, void *buffer, int offset, int size) {
	
}

void cache_read(sid_t index, void *buffer, int offset, int size) {

}

void cache_close(void) {

}

void cache_dump(void) {

}

void cache_main(void) {

}

void cache_init(void) {

}
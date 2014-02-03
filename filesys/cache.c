#include "cache.h"
#include <threads/synch.h>
/**
	compilation options
*/
#define CACHE_SIZE_IN_SECTORS 64
#define SECTOR_SIZE_IN_BYTES 512


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
};
typedef struct buffer_cache buffer_cache;

buffer_cache gCache;



void write(sid_t index, void *buffer, int offset, int size) {

}

void read(sid_t index, void *buffer, int offset, int size) {

}

void dump_cache(void) {

}
#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H


/*
sector index type
*/
typedef int sid_t;

/**
	will write to disk through the cache
*/
void cache_write(sid_t index, void *buffer, int offset, int size);


/**
	will read from disk through the cache
*/
void cache_read(sid_t index, void *buffer, int offset, int size);

/**
	called when the OS starts.
	- starts the cache main thread
*/
void cache_init(void);

/**
	called when the OS closes. Will write unwritten data to disk
*/
void cache_close(void);
#endif

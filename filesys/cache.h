#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H


/*
sector index type
*/
typedef int sid_t;

void write(sid_t index, void *buffer, int offset, int size);
void read(sid_t index, void *buffer, int offset, int size);
void dump_cache(void);


#endif
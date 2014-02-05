#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <stdbool.h>

bool path_is_relative(const char *path);
void path_next_entry(const char *path, char *entry, int max_entry_length, int *offset);
void path_split_last(const char *path, char *sub_path, char *last_entry, int max_entry_length);
#endif

#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <stdbool.h>

bool path_is_relative(char *path);
void path_next_entry(char *path, char *entry, int max_entry_length, int *offset);

#endif
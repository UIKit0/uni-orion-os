#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #define TEST_PATH

#ifdef TEST_PATH
#include <assert.h>
#endif

bool path_is_relative(char *path) {
	if (path[0] != '\0' && path[0] != '/') {
		return true;
	}
	return false;
}

void path_next_entry(char *path, char *entry, int max_entry_length, int *offset) {
	int length = 0;
	char *entry_iterator = entry;
	char *path_iterator = path;

	// skip leading slashes
	while (*path_iterator == '/') {
		path_iterator += 1;
		*offset += 1;
	}

	while (*path_iterator != '\0' && *path_iterator != '/' && length < max_entry_length) {
		*(entry_iterator++) = *(path_iterator++);
		length += 1;
		*offset += 1;
	}

	while (*path_iterator != '\0' && *path_iterator == '/') {
		path_iterator++;
		*offset += 1;
	}

	*entry_iterator = '\0';
}

void path_split_last(char *path, char *sub_path, char *last_entry, int max_entry_length) {
	char *entry = (char*)malloc(max_entry_length);
	int offset = 0;
	int last_offset = 0;

	while (*(path + offset) != '\0') {
		last_offset = offset;
		path_next_entry(path + offset, entry, max_entry_length, &offset);
	}

	if (last_offset == 0) {
		last_offset = 1;
	}

#ifdef TEST_PATH
	strncpy(last_entry, entry, max_entry_length);
	strncpy(sub_path, path, last_offset - 1);
#else
	strlcpy(last_entry, entry, max_entry_length);
	strlcpy(sub_path, path, last_offset);
#endif
	sub_path[last_offset - 1] = '\0';
}

#ifdef TEST_PATH
void test_path_parse() {
	char *path = "/home/../..///alex/sources/../pintos/doc.pdf";
	char *entries[8] = {"home", "..", "..", "alex", "sources", "..", "pintos", "doc.pdf"};

	char entry[14];
	int offset = 0;

	path_next_entry(path, entry, 14, &offset);
	int index = 0;
	while (*(path + offset) != '\0') {
		assert(strcmp(entries[index], entry) == 0);
		path_next_entry(path + offset, entry, 14, &offset);
		index += 1;
	}

	assert(index == 7);
	assert(strcmp(entries[index], entry) == 0);
}

void test_path_type() {
	assert(path_is_relative("/home/alex") == false);
	assert(path_is_relative("home/alex") == true);
	assert(path_is_relative("./home/alex") == true);
	assert(path_is_relative("../home/alex") == true);
}

void test_split_last() {
	char sub_path[256], last_entry[14];

	path_split_last("/home/alex/pintos/filesys/directory.c", sub_path, last_entry, 14);
	assert(strcmp("/home/alex/pintos/filesys", sub_path) == 0);
	assert(strcmp("directory.c", last_entry) == 0);

	path_split_last("../home/alex/pintos/filesys/directory.c", sub_path, last_entry, 14);
	assert(strcmp("../home/alex/pintos/filesys", sub_path) == 0);
	assert(strcmp("directory.c", last_entry) == 0);

	path_split_last("../home/alex/pintos/filesys/", sub_path, last_entry, 14);
	assert(strcmp("../home/alex/pintos", sub_path) == 0);
	assert(strcmp("filesys", last_entry) == 0);

	path_split_last("/home/alex/pintos/filesys////", sub_path, last_entry, 14);
	assert(strcmp("/home/alex/pintos", sub_path) == 0);
	assert(strcmp("filesys", last_entry) == 0);

	path_split_last("test", sub_path, last_entry, 14);
	assert(strcmp("", sub_path) == 0);
	assert(strcmp("test", last_entry) == 0);

	path_split_last("./test", sub_path, last_entry, 14);
	assert(strcmp(".", sub_path) == 0);
	assert(strcmp("test", last_entry) == 0);

	path_split_last("/test", sub_path, last_entry, 14);
	assert(strcmp("", sub_path) == 0);
	assert(strcmp("test", last_entry) == 0);
}

int main(void) {
	test_path_type();
	test_path_parse();
	test_split_last();
	return 0;
}
#endif

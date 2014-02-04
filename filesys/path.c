#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #define TEST_PATH

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
	char entry[14];
	int offset = 0;

	path_next_entry(path, entry, 14, &offset);
	int x = 0;
	while (*(path + offset) != '\0' && (x++) < 11) {
		printf("%s %d\n", entry, offset);
		path_next_entry(path + offset, entry, 14, &offset);
	}
}

void test_path_type() {
	printf("%d\n", path_is_relative("/home/alex"));
	printf("%d\n", path_is_relative("home/alex"));
	printf("%d\n", path_is_relative("./home/alex"));
	printf("%d\n", path_is_relative("../home/alex"));
}

void test_split_last() {
	char *path_file = "/home/alex/pintos/filesys/directory.c";
	char *path_relative = "../home/alex/pintos/filesys/directory.c";
	char *path_dir = "../home/alex/pintos/filesys/";
	char *path_dir_multiple = "/home/alex/pintos/filesys////";


	char sub_path[256];
	char last_entry[14];

	path_split_last(path_file, sub_path, last_entry, 14);
	printf("%s %s \n", sub_path, last_entry);

	path_split_last(path_relative, sub_path, last_entry, 14);
	printf("%s %s \n", sub_path, last_entry);

	path_split_last(path_dir, sub_path, last_entry, 14);
	printf("%s %s \n", sub_path, last_entry);

	path_split_last(path_dir_multiple, sub_path, last_entry, 14);
	printf("%s %s \n", sub_path, last_entry);
}

int main(void) {
	// test_path_parse();
	// test_path_type();
	test_split_last();
	return 0;
}
#endif

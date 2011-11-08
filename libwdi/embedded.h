#pragma once
const unsigned char file_000[] = {0, 0, 0, 0, 0, 0};

struct res {
	char* subdir;
	char* name;
	size_t size;
	int64_t creation_time;
	const unsigned char* data;
};

const struct res resource[] = {
	{ "x86", "dummy", 6, INT64_C(1316423913), file_000 },
};

const int nb_resources = sizeof(resource)/sizeof(resource[0]);

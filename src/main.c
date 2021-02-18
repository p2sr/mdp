#include <stdio.h>

#include "common.h"
#include "demo.h"

FILE *g_errfile;

int main(void) {
	g_errfile = stderr;

	struct demo *demo = demo_parse("test.dem");

	if (demo) {
		puts("Successfully parsed demo!");
	} else {
		puts("Failed to parse demo!");
	}

	demo_free(demo);

	return 0;
}

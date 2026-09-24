#include "Yarfwm.hpp"
#include <cstdio>

int main(int argc, char *argv[])
{
	Yarfwm application;

	if (!application.initialize(argc, argv)) {
		fprintf(stderr, "Yarfwm: initialization failed\n");
		return 1;
	}

	return application.run();
}

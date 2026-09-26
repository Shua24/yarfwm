#include "SpawnedProcesses.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

// Room for the whole list of children. The list is one line of decimal
// process ids; a window manager has a handful, so a generous fixed buffer
// keeps this allocation-free.
static const std::size_t child_list_capacity = 4096;

void read_child_process_ids(std::vector<pid_t> *child_process_ids)
{
	child_process_ids->clear();

	char path[64];
	// Dynamic memory: none. The thread id is formatted into a fixed buffer.
	std::snprintf(path, sizeof(path), "/proc/%d/task/%d/children",
		      static_cast<int>(getpid()), static_cast<int>(getpid()));

	const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		return;
	}

	char buffer[child_list_capacity];
	const ssize_t bytes_read = read(descriptor, buffer, sizeof(buffer) - 1);
	close(descriptor);

	if (bytes_read <= 0) {
		return;
	}
	buffer[bytes_read] = '\0';

	// Dynamic memory: the caller's vector grows as entries are parsed; it
	// is freed by the caller, which owns it across calls.
	char *cursor = buffer;
	while (*cursor != '\0') {
		// strtol skips the leading spaces and stops at the next one.
		char *after_number = nullptr;
		const long value = std::strtol(cursor, &after_number, 10);
		if (after_number == cursor) {
			// Not a number: nothing left worth parsing.
			break;
		}
		if (value > 0) {
			child_process_ids->push_back(static_cast<pid_t>(value));
		}
		cursor = after_number;
	}
}

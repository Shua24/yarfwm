// yarfwm -- the titlebar's backing store.
//
// A wl_shm pool is backed by a file descriptor, so the buffer needs a real fd
// behind it. memfd_create gives one with no filesystem name and no cleanup;
// where it is unavailable (or fails) a mkstemp in XDG_RUNTIME_DIR is used and
// unlinked immediately, so nothing is left behind on crash either way.
//
// The order matters and is not arbitrary:
//   ftruncate -> create_pool -> create_buffer -> mmap -> destroy the pool.
// The buffer keeps the pool's memory alive, so the pool proxy is destroyed as
// soon as the buffer exists; the fd stays open because the mapping and the
// compositor both reference it.
//
// create() never leaves the object half-built: on any failure the previous
// state is untouched, and destroy() is safe to call twice or on a default
// constructed object.
#include "ShmBuffer.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client-protocol.h>

namespace
{

constexpr int32_t bytes_per_pixel = 4;

// One shared-memory file descriptor of exactly `size` bytes, or -1.
int create_pool_fd(int32_t size)
{
	int fd = -1;
#ifdef __linux__
	fd = memfd_create("yarfwm-shm", MFD_CLOEXEC);
#endif
	if (fd >= 0) {
		if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
			close(fd);
			return -1;
		}
		return fd;
	}

	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == nullptr || runtime_dir[0] == '\0') {
		// There is no safe fallback path here: a world-writable
		// directory would be a security problem and the project
		// forbids /tmp for scratch. Fail honestly instead.
		return -1;
	}
	std::string template_path(runtime_dir);
	template_path += "/yarfwm-shm-XXXXXX";
	// mkstemp needs a mutable, NUL-terminated buffer.
	std::string mutable_path(template_path);
	fd = mkstemp(mutable_path.data());
	if (fd < 0) {
		return -1;
	}
	unlink(mutable_path.c_str());
	if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

} // namespace

ShmBuffer::ShmBuffer()
    : shared_memory(nullptr), shm_buffer(nullptr), mapped_pixels(nullptr),
      buffer_width(0), buffer_height(0), mapped_size(0),
      pool_file_descriptor(-1)
{
}

ShmBuffer::~ShmBuffer() { destroy(); }

bool ShmBuffer::is_valid() const
{
	return mapped_pixels != nullptr && shm_buffer != nullptr &&
	       buffer_width > 0 && buffer_height > 0;
}

bool ShmBuffer::create(struct wl_shm *shared_memory_arg, int32_t width,
		       int32_t height)
{
	if (shared_memory_arg == nullptr || width <= 0 || height <= 0) {
		return false;
	}
	if (is_valid() && width == buffer_width && height == buffer_height &&
	    shared_memory == shared_memory_arg) {
		return true; // the documented no-op
	}

	const int32_t stride = width * bytes_per_pixel;
	const int32_t size = stride * height;

	const int fd = create_pool_fd(size);
	if (fd < 0) {
		return false;
	}
	struct wl_shm_pool *pool =
	    wl_shm_create_pool(shared_memory_arg, fd, size);
	if (pool == nullptr) {
		close(fd);
		return false;
	}
	struct wl_buffer *new_buffer = wl_shm_pool_create_buffer(
	    pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool); // the buffer keeps the pool alive
	if (new_buffer == nullptr) {
		close(fd);
		return false;
	}
	void *mapping = mmap(nullptr, static_cast<size_t>(size),
			     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		wl_buffer_destroy(new_buffer);
		close(fd);
		return false;
	}

	// Everything succeeded: only now does the previous state go away.
	destroy();
	shared_memory = shared_memory_arg;
	shm_buffer = new_buffer;
	mapped_pixels = static_cast<uint32_t *>(mapping);
	buffer_width = width;
	buffer_height = height;
	mapped_size = size;
	pool_file_descriptor = fd;
	return true;
}

void ShmBuffer::destroy()
{
	if (mapped_pixels != nullptr && mapped_size > 0) {
		munmap(static_cast<void *>(mapped_pixels),
		       static_cast<size_t>(mapped_size));
	}
	mapped_pixels = nullptr;
	mapped_size = 0;

	if (shm_buffer != nullptr) {
		wl_buffer_destroy(shm_buffer);
	}
	shm_buffer = nullptr;

	if (pool_file_descriptor >= 0) {
		close(pool_file_descriptor);
	}
	pool_file_descriptor = -1;

	shared_memory = nullptr;
	buffer_width = 0;
	buffer_height = 0;
}

void ShmBuffer::attach_and_commit(struct wl_surface *surface) const
{
	if (surface == nullptr || shm_buffer == nullptr) {
		return;
	}
	wl_surface_attach(surface, shm_buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, buffer_width, buffer_height);
	wl_surface_commit(surface);
}

void ShmBuffer::detach(struct wl_surface *surface) const
{
	if (surface == nullptr) {
		return;
	}
	// A NULL buffer is explicitly allowed by wl_surface.attach and unmaps
	// the surface, so nothing of it is drawn any more. A zero-sized damage
	// region is not enough on its own: the surface would keep showing the
	// last attached buffer.
	wl_surface_attach(surface, nullptr, 0, 0);
	wl_surface_commit(surface);
}

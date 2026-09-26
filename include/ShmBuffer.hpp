#ifndef SHM_BUFFER_HPP
#define SHM_BUFFER_HPP

#include <cstdint>

struct wl_shm;
struct wl_buffer;
struct wl_surface;

// One shared-memory pixel buffer, plus the mapping the window manager draws
// into. This is the whole of yarfwm's rendering surface: a wl_shm pool with a
// single ARGB8888 buffer, mmap'ed so cairo can paint straight into it.
//
// Dynamic memory, documented per INSTRUCTIONS.md:47:
//   - the pool is a file descriptor backed by a memfd (or a temporary file
//     when memfd_create is unavailable), created here and closed here;
//   - the mapping is one mmap of width*height*4 bytes, unmapped in destroy();
//   - the wl_buffer proxy is created by the compositor and destroyed in
//     destroy(). Nothing is heap-allocated with new/delete.
class ShmBuffer
{
      public:
	ShmBuffer();
	~ShmBuffer();

	// Create (or resize) the pool and buffer for the given size. Returns
	// false on any failure, leaving the object in its previous state. A
	// repeat call with the same size is a no-op that returns true.
	bool create(struct wl_shm *shared_memory, int32_t width,
		    int32_t height);
	void destroy();

	bool is_valid() const;
	int32_t width() const { return buffer_width; }
	int32_t height() const { return buffer_height; }

	// The mapped pixels, ARGB8888 (premultiplied), stride = width * 4.
	// Never null while is_valid() is true.
	uint32_t *pixels() const { return mapped_pixels; }
	int32_t stride() const { return buffer_width * 4; }

	// Attach to a surface and commit. The caller must have finished
	// painting before this: the compositor may sample the pixels until the
	// next commit.
	void attach_and_commit(struct wl_surface *surface) const;

	// Attach a NULL buffer and commit, so the surface stops showing
	// anything. A wl_surface with no buffer mapped is composited as empty,
	// which is how a decoration that has lost the room to paint stops
	// covering the window underneath it. The buffer itself is kept, so a
	// later attach_and_commit() reuses it.
	void detach(struct wl_surface *surface) const;

	struct wl_buffer *buffer() const { return shm_buffer; }

      private:
	struct wl_shm *shared_memory;
	struct wl_buffer *shm_buffer;
	uint32_t *mapped_pixels;
	int32_t buffer_width;
	int32_t buffer_height;
	int32_t mapped_size;
	int pool_file_descriptor;
};

#endif // SHM_BUFFER_HPP

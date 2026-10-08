/* kept_pictures.hpp - the pictures of frames already drawn, kept by frame.
 *
 * A machine put back on a frame it has been on before draws that frame's
 * picture again, and a renderer on the far side of the GPU bridge draws it
 * wrong for a while after a state load: its textures and buffers are rebuilt
 * as the game touches them, and until it has, what comes back is pieces of
 * the picture before the load (chimera#187, chimera#190). The picture a frame
 * had the first time is the same picture, so it is kept here - compressed,
 * within a budget of its own, the least recently used going first - and shown
 * instead.
 *
 * Nothing here is part of a savestate, a history file or a movie: it is what
 * was on the screen, and a session that has none shows what the core draws,
 * as it always did.
 *
 * Packing a picture is the cost - milliseconds for a large one, measured in
 * test_kept_pictures.cpp - and the frame that was just shown has none to
 * spare, so it is done on a helper thread (work_thread.hpp, and by its
 * rules): keep() copies the picture and posts it, the helper packs the copy
 * and leaves the result in an outbox, and the result is taken in the next
 * time this is asked anything. A caller sees none of that: a frame asked for
 * while its picture is still being packed is waited for, anything that
 * forgets pictures waits for what is in flight first, and when the helper is
 * three pictures behind the next one is simply not kept.
 */
#pragma once

#include "work_thread.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

class CeKeptPictures
{
public:
	/* 0 switches it off and forgets everything. */
	void budget(uint64_t bytes);
	uint64_t budget() const { return m_budget; }
	uint64_t bytes() { settle(); return m_bytes; }
	int64_t count() { settle(); return static_cast<int64_t>(m_byFrame.size()); }
	bool has(int64_t frame) { settle(); return m_byFrame.count(frame) != 0; }

	/* A picture larger than this is not kept: copying it would itself cost
	 * the frame too much. 16 MiB is 2560x1600. */
	static constexpr size_t MaxRawBytes = size_t{ 16 } << 20;

	/* Keeps a frame's picture (BGRA, w x h), replacing one it had. False when
	 * it is off, the picture is empty or would not fit the whole budget, or
	 * there is no compressor to be had. */
	bool keep(int64_t frame, const uint32_t *bgra, int32_t w, int32_t h);

	/* The picture kept for a frame, into `out` (at least capacity pixels):
	 * true, with its size, when there is one and it fits. */
	bool show(int64_t frame, uint32_t *out, size_t capacity, int32_t *w, int32_t *h);

	/* The timeline changed after this frame: what was drawn past it was drawn
	 * for input that is no longer the movie's. */
	void dropAfter(int64_t frame);
	void drop(int64_t frame);
	void clear();

private:
	struct Picture
	{
		int32_t w = 0, h = 0;
		std::vector<uint8_t> packed;
		uint64_t used = 0; /* when it was last kept or shown */
	};
	/* what the helper hands back: owned by whoever holds it, never the store's */
	struct Packed
	{
		int64_t frame = 0;
		int32_t w = 0, h = 0;
		std::vector<uint8_t> bytes; /* empty: it could not be packed */
	};
	struct Outbox
	{
		std::mutex lock;
		std::vector<Packed> done;
	};

	std::map<int64_t, Picture> m_byFrame;
	std::map<uint64_t, int64_t> m_byUse; /* oldest first */
	uint64_t m_budget = 0, m_bytes = 0, m_clock = 0;
	std::shared_ptr<Outbox> m_outbox = std::make_shared<Outbox>();
	std::set<int64_t> m_packing; /* frames posted and not taken in yet */
	chimera::WorkThread m_helper{ "pictures" };

	void touch(int64_t frame, Picture &p);
	void evict();
	void takeIn();  /* what the helper has finished */
	void settle();  /* ...after waiting for what it has not */
};

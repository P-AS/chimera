/* kept_pictures.cpp - see kept_pictures.hpp */
#include "kept_pictures.hpp"

#include "zstd_dyn.hpp"

#include <cstring>

void CeKeptPictures::budget(uint64_t bytes)
{
	settle();
	m_budget = bytes;
	if (bytes == 0) clear();
	else evict();
}

/* What the helper has finished goes into the store. The helper never touches
 * the store itself: it leaves results in the outbox, and they are taken in
 * here, on the thread that owns everything else. */
void CeKeptPictures::takeIn()
{
	std::vector<Packed> done;
	{
		std::lock_guard<std::mutex> hold(m_outbox->lock);
		done.swap(m_outbox->done);
	}
	for (Packed &r : done)
	{
		m_packing.erase(r.frame);
		if (r.bytes.empty() || r.bytes.size() > m_budget) continue;
		drop(r.frame);
		Picture &p = m_byFrame[r.frame];
		p.w = r.w;
		p.h = r.h;
		m_bytes += r.bytes.size();
		p.packed = std::move(r.bytes);
		touch(r.frame, p);
	}
	if (!done.empty()) evict();
}

void CeKeptPictures::settle()
{
	if (!m_packing.empty()) m_helper.drain();
	takeIn();
}

void CeKeptPictures::clear()
{
	settle();
	m_byFrame.clear();
	m_byUse.clear();
	m_bytes = 0;
}

void CeKeptPictures::touch(int64_t frame, Picture &p)
{
	if (p.used != 0) m_byUse.erase(p.used);
	p.used = ++m_clock;
	m_byUse[p.used] = frame;
}

void CeKeptPictures::drop(int64_t frame)
{
	if (m_packing.count(frame) != 0) settle();
	auto it = m_byFrame.find(frame);
	if (it == m_byFrame.end()) return;
	m_bytes -= it->second.packed.size();
	m_byUse.erase(it->second.used);
	m_byFrame.erase(it);
}

void CeKeptPictures::dropAfter(int64_t frame)
{
	settle(); /* a picture still being packed is one of those to forget */
	while (!m_byFrame.empty() && m_byFrame.rbegin()->first > frame) drop(m_byFrame.rbegin()->first);
}

void CeKeptPictures::evict()
{
	while (m_bytes > m_budget && !m_byUse.empty()) drop(m_byUse.begin()->second);
}

bool CeKeptPictures::keep(int64_t frame, const uint32_t *bgra, int32_t w, int32_t h)
{
	if (m_budget == 0 || bgra == nullptr || w <= 0 || h <= 0) return false;
	const size_t raw = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint32_t);
	if (raw > MaxRawBytes) return false;
	const char *why = nullptr;
	const chimera::ZstdApi *z = chimera::zstdApi(&why);
	if (z == nullptr) return false;
	takeIn();
	/* the helper is behind: this frame's picture is not worth making it later */
	if (m_helper.pending() >= 3) return false;
	if (m_packing.count(frame) != 0) settle(); /* the same frame twice in a row: in order */

	auto copy = std::make_shared<std::vector<uint32_t>>(bgra, bgra + static_cast<size_t>(w) * static_cast<size_t>(h));
	std::shared_ptr<Outbox> outbox = m_outbox;
	m_packing.insert(frame);
	m_helper.post([z, copy, outbox, frame, w, h, raw]() {
		Packed r;
		r.frame = frame;
		r.w = w;
		r.h = h;
		std::vector<uint8_t> room(z->compressBound(raw));
		const size_t packed = z->compress(room.data(), room.size(), copy->data(), raw, 1);
		if (z->isError(packed) == 0 && packed != 0)
		{
			room.resize(packed);
			room.shrink_to_fit();
			r.bytes = std::move(room);
		}
		std::lock_guard<std::mutex> hold(outbox->lock);
		outbox->done.push_back(std::move(r));
	});
	return true;
}

bool CeKeptPictures::show(int64_t frame, uint32_t *out, size_t capacity, int32_t *w, int32_t *h)
{
	/* a frame whose picture is still being packed is waited for; any other is not */
	if (m_packing.count(frame) != 0) settle();
	else takeIn();
	auto it = m_byFrame.find(frame);
	if (it == m_byFrame.end() || out == nullptr) return false;
	Picture &p = it->second;
	const size_t pixels = static_cast<size_t>(p.w) * static_cast<size_t>(p.h);
	if (pixels > capacity) return false;
	const char *why = nullptr;
	const chimera::ZstdApi *z = chimera::zstdApi(&why);
	if (z == nullptr) return false;
	void *stream = z->createDStream();
	if (stream == nullptr) return false;
	z->initDStream(stream);
	chimera::ZstdApi::Buffer in{ p.packed.data(), p.packed.size(), 0 };
	chimera::ZstdApi::OutBuffer to{ out, pixels * sizeof(uint32_t), 0 };
	bool ok = true;
	while (in.pos < in.size && ok)
	{
		const size_t before = in.pos + to.pos;
		const size_t r = z->decompressStream(stream, &to, &in);
		ok = z->isError(r) == 0 && in.pos + to.pos != before;
	}
	z->freeDStream(stream);
	if (!ok || to.pos != pixels * sizeof(uint32_t))
	{
		drop(frame); /* a picture that does not come back is not one to offer again */
		return false;
	}
	if (w != nullptr) *w = p.w;
	if (h != nullptr) *h = p.h;
	touch(frame, p);
	return true;
}

#pragma once
/* The machine's picture on its way to the window. The emulation thread hands
 * in each frame; the window copies out the newest one when it next draws. */

#include <cstdint>
#include <mutex>
#include <vector>

namespace chimera::vm {

class Picture
{
public:
	struct Frame
	{
		int32_t width = 0, height = 0;
		/* BGRA, width * height, every A 0xFF */
		std::vector<uint32_t> pixels;
		/* the shape the picture is shown at, width over height */
		double aspect = 4.0 / 3.0;
		uint64_t serial = 0;
	};

	/* any thread */
	void publish(const uint32_t *pixels, int32_t width, int32_t height, double aspect)
	{
		std::lock_guard<std::mutex> lock(m_);
		const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
		frame_.pixels.resize(n);
		/* the fourth byte is the core's to leave as it likes, and the window
		 * draws it as it comes: a desktop that takes it as see-through
		 * (Wayland's) showed what was behind the window through the picture
		 * (issue 393) */
		for (size_t i = 0; i < n; i++) frame_.pixels[i] = pixels[i] | 0xFF000000u;
		frame_.width = width;
		frame_.height = height;
		frame_.aspect = aspect;
		frame_.serial++;
	}

	/* any thread: copies the newest frame out when it is newer than `have` */
	bool takeIfNewer(uint64_t have, Frame &out) const
	{
		std::lock_guard<std::mutex> lock(m_);
		if (frame_.serial == have) return false;
		out.width = frame_.width;
		out.height = frame_.height;
		out.aspect = frame_.aspect;
		out.serial = frame_.serial;
		out.pixels.assign(frame_.pixels.begin(), frame_.pixels.end());
		return true;
	}

private:
	mutable std::mutex m_;
	Frame frame_;
};

} // namespace chimera::vm

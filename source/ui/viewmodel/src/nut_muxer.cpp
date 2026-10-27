#include "chimera/vm/nut_muxer.hpp"

#include <cstring>
#include <numeric>
#include <string>

namespace chimera::vm {

namespace {
constexpr uint64_t kMain = 0x4E4D7A561F5F04ADull, kStream = 0x4E5311405BF2F9DBull, kSyncpoint = 0x4E4BE4ADEECA4569ull;

void varU(std::vector<uint8_t> &out, uint64_t v)
{
	uint8_t b[10];
	int i = 0;
	do
	{
		b[i] = static_cast<uint8_t>((v & 127) | (i > 0 ? 128 : 0));
		i++;
		v /= 128;
	} while (v > 0);
	for (; i > 0; i--) out.push_back(b[i - 1]);
}

void bytes(std::vector<uint8_t> &out, const void *data, size_t size)
{
	varU(out, size);
	const uint8_t *p = static_cast<const uint8_t *>(data);
	out.insert(out.end(), p, p + size);
}

void be64(std::vector<uint8_t> &out, uint64_t v)
{
	for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void be32(std::vector<uint8_t> &out, uint32_t v)
{
	for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

uint32_t nutCrc(const uint8_t *p, size_t n)
{
	static const uint32_t table[16] = {0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
		0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD};
	uint32_t crc = 0;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= static_cast<uint32_t>(p[i]) << 24;
		crc = (crc << 4) ^ table[crc >> 28];
		crc = (crc << 4) ^ table[crc >> 28];
	}
	return crc;
}

/* a packet: its start code, its size, a checksum of the header when it is
 * long, the data, and the data's checksum */
void packet(std::vector<uint8_t> &out, uint64_t startCode, const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> header;
	be64(header, startCode);
	varU(header, data.size() + 4);
	if (data.size() > 4092) be32(header, nutCrc(header.data(), header.size()));
	out.insert(out.end(), header.begin(), header.end());
	out.insert(out.end(), data.begin(), data.end());
	be32(out, nutCrc(data.data(), data.size()));
}
} // namespace

NutMuxer::NutMuxer(int32_t width, int32_t height, int64_t fpsNum, int64_t fpsDen, int32_t sampleRate, int32_t channels, Sink sink,
	int64_t sarNum, int64_t sarDen)
	: sink_(std::move(sink)), fpsNum_(fpsNum), fpsDen_(fpsDen), sampleRate_(sampleRate), channels_(channels)
{
	/* the time bases must be relatively prime */
	const int64_t g = std::gcd(fpsNum_, fpsDen_);
	if (g > 1)
	{
		fpsNum_ /= g;
		fpsDen_ /= g;
	}
	std::vector<uint8_t> out;
	static const char magic[] = "nut/multimedia container";
	out.insert(out.end(), magic, magic + sizeof magic); /* with its zero */
	std::vector<uint8_t> main;
	varU(main, 3);                         /* version */
	varU(main, 2);                         /* stream count */
	varU(main, 65536);                     /* max distance */
	varU(main, 2);                         /* time base count */
	varU(main, static_cast<uint64_t>(fpsDen_)); /* the picture's time base */
	varU(main, static_cast<uint64_t>(fpsNum_));
	varU(main, 1); /* the sound's */
	varU(main, static_cast<uint64_t>(sampleRate_));
	/* 255, not 256: 0x4e would read as a start code */
	for (int i = 0; i < 255; i++)
	{
		varU(main, 1 << 12); /* FLAG_CODED */
		varU(main, 0);
	}
	varU(main, 0); /* header count minus 1 */
	varU(main, 0); /* main flags */
	packet(out, kMain, main);
	std::vector<uint8_t> video;
	varU(video, 0); /* stream id */
	varU(video, 0); /* video */
	bytes(video, "BGRA", 4);
	varU(video, 0); /* time base 0 */
	varU(video, 8); /* msb pts shift */
	varU(video, 1); /* max pts distance */
	varU(video, 0); /* decode delay */
	varU(video, 1); /* FLAG_FIXED_FPS */
	bytes(video, "", 0);
	varU(video, static_cast<uint64_t>(width));
	varU(video, static_cast<uint64_t>(height));
	/* a pixel's shape: a picture shown wider or narrower than its pixels
	 * (a PC-98's 640x400 shown 4:3) plays in that shape (issue 375) */
	varU(video, static_cast<uint64_t>(sarNum > 0 && sarDen > 0 ? sarNum : 1)); /* sample width and height */
	varU(video, static_cast<uint64_t>(sarNum > 0 && sarDen > 0 ? sarDen : 1));
	varU(video, 18); /* full range rec709 */
	packet(out, kStream, video);
	std::vector<uint8_t> audio;
	varU(audio, 1); /* stream id */
	varU(audio, 1); /* audio */
	static const uint8_t pcm[4] = {1, 0, 0, 0};
	bytes(audio, pcm, 4);
	varU(audio, 1); /* time base 1 */
	varU(audio, 8);
	varU(audio, static_cast<uint64_t>(sampleRate_));
	varU(audio, 0);
	varU(audio, 0); /* not fixed: the chunks need not be the same size */
	bytes(audio, "", 0);
	varU(audio, static_cast<uint64_t>(sampleRate_));
	varU(audio, 1);
	varU(audio, static_cast<uint64_t>(channels_));
	packet(out, kStream, audio);
	emit(out);
}

NutMuxer::Frame NutMuxer::makeFrame(const uint8_t *payload, size_t size, uint64_t pts, uint64_t num, uint64_t den, int streamIndex)
{
	Frame f;
	f.pts = pts;
	f.num = num;
	f.den = den;
	std::vector<uint8_t> sync;
	varU(sync, pts * 2 + static_cast<uint64_t>(streamIndex)); /* global key pts */
	varU(sync, 1);                                           /* back pointer */
	packet(f.bytes, kSyncpoint, sync);
	std::vector<uint8_t> header;
	header.push_back(0); /* frame code */
	uint64_t flags = 1 << 0 /* key */ | 1 << 3 /* coded pts */ | 1 << 4 /* stream id */ | 1 << 5 /* size msb */ | 1 << 6 /* checksum */;
	if (size == 0) flags |= 1 << 1; /* end of relevance */
	varU(header, flags);
	varU(header, static_cast<uint64_t>(streamIndex));
	varU(header, pts + 256); /* pts + 1 << msb pts shift */
	varU(header, size);
	f.bytes.insert(f.bytes.end(), header.begin(), header.end());
	be32(f.bytes, nutCrc(header.data(), header.size()));
	if (size > 0) f.bytes.insert(f.bytes.end(), payload, payload + size);
	return f;
}

/* whether a comes no later than b, in time */
bool NutMuxer::atOrBefore(const Frame &a, const Frame &b)
{
	return static_cast<unsigned __int128>(a.pts) * a.num * b.den <= static_cast<unsigned __int128>(b.pts) * b.num * a.den;
}

bool NutMuxer::emit(const std::vector<uint8_t> &out)
{
	if (failed_) return false;
	if (!sink_(out.data(), out.size())) failed_ = true;
	return !failed_;
}

bool NutMuxer::writeVideo(const uint32_t *pixels, size_t count)
{
	if (videoDone_) return false;
	if (count == 0) videoDone_ = true;
	Frame f = makeFrame(reinterpret_cast<const uint8_t *>(pixels), count * 4, videoPts_++, static_cast<uint64_t>(fpsDen_),
		static_cast<uint64_t>(fpsNum_), 0);
	videoQueue_.push_back(std::move(f));
	const Frame &mine = videoQueue_.back();
	while (!audioQueue_.empty() && atOrBefore(audioQueue_.front(), mine))
	{
		if (!emit(audioQueue_.front().bytes)) return false;
		audioQueue_.pop_front();
	}
	return !failed_;
}

bool NutMuxer::writeAudio(const int16_t *samples, size_t count)
{
	if (audioDone_) return false;
	if (count == 0) audioDone_ = true;
	Frame f = makeFrame(reinterpret_cast<const uint8_t *>(samples), count * 2, audioPts_, 1, static_cast<uint64_t>(sampleRate_), 1);
	audioPts_ += count / static_cast<size_t>(channels_);
	audioQueue_.push_back(std::move(f));
	const Frame &mine = audioQueue_.back();
	while (!videoQueue_.empty() && atOrBefore(videoQueue_.front(), mine))
	{
		if (!emit(videoQueue_.front().bytes)) return false;
		videoQueue_.pop_front();
	}
	return !failed_;
}

bool NutMuxer::finish()
{
	if (!videoDone_) writeVideo(nullptr, 0);
	if (!audioDone_) writeAudio(nullptr, 0);
	while (!audioQueue_.empty() && !videoQueue_.empty())
	{
		std::deque<Frame> &q = atOrBefore(audioQueue_.front(), videoQueue_.front()) ? audioQueue_ : videoQueue_;
		if (!emit(q.front().bytes)) return false;
		q.pop_front();
	}
	for (std::deque<Frame> *q : {&audioQueue_, &videoQueue_})
		while (!q->empty())
		{
			if (!emit(q->front().bytes)) return false;
			q->pop_front();
		}
	return !failed_;
}

} // namespace chimera::vm

#include "chimera/vm/sound.hpp"

#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace chimera::vm {

namespace {
/* OpenAL's stable C interface, the little of it used here */
constexpr int AL_FORMAT_STEREO16 = 0x1103, AL_BUFFERS_PROCESSED = 0x1016, AL_BUFFERS_QUEUED = 0x1015,
	AL_SOURCE_STATE = 0x1010, AL_PLAYING = 0x1012;
/* buffers in flight: one a frame, so this is how many frames may wait */
constexpr int kBuffers = 16;
/* more than this many frames' sound waiting, and the next frame's is dropped */
constexpr int kMostAhead = 6;
} // namespace

struct Sound::Api
{
	void *lib = nullptr;
	void *(*alcOpenDevice)(const char *) = nullptr;
	int (*alcCloseDevice)(void *) = nullptr;
	void *(*alcCreateContext)(void *, const int *) = nullptr;
	void (*alcDestroyContext)(void *) = nullptr;
	int (*alcMakeContextCurrent)(void *) = nullptr;
	void (*alGenSources)(int, unsigned *) = nullptr;
	void (*alDeleteSources)(int, const unsigned *) = nullptr;
	void (*alGenBuffers)(int, unsigned *) = nullptr;
	void (*alDeleteBuffers)(int, const unsigned *) = nullptr;
	void (*alBufferData)(unsigned, int, const void *, int, int) = nullptr;
	void (*alSourceQueueBuffers)(unsigned, int, const unsigned *) = nullptr;
	void (*alSourceUnqueueBuffers)(unsigned, int, unsigned *) = nullptr;
	void (*alGetSourcei)(unsigned, int, int *) = nullptr;
	void (*alSourcePlay)(unsigned) = nullptr;
	void (*alSourceStop)(unsigned) = nullptr;
	void *device = nullptr, *context = nullptr;
	unsigned source = 0;
	unsigned buffers[kBuffers] = {};
	std::vector<unsigned> free;
	bool started = false;

	template <class F>
	bool bind(F &f, const char *name)
	{
#ifdef _WIN32
		f = reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
		f = reinterpret_cast<F>(dlsym(lib, name));
#endif
		return f != nullptr;
	}
};

Sound::Sound() : api_(std::make_unique<Api>())
{
	Api &a = *api_;
#ifdef _WIN32
	a.lib = LoadLibraryW(L"OpenAL32.dll");
#else
	a.lib = dlopen("libopenal.so", RTLD_NOW | RTLD_LOCAL);
	if (a.lib == nullptr) a.lib = dlopen("libopenal.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
	if (a.lib == nullptr)
	{
		problem_ = "OpenAL was not found, so there is no sound";
		return;
	}
	if (!(a.bind(a.alcOpenDevice, "alcOpenDevice") && a.bind(a.alcCloseDevice, "alcCloseDevice") &&
		a.bind(a.alcCreateContext, "alcCreateContext") && a.bind(a.alcDestroyContext, "alcDestroyContext") &&
		a.bind(a.alcMakeContextCurrent, "alcMakeContextCurrent") && a.bind(a.alGenSources, "alGenSources") &&
		a.bind(a.alDeleteSources, "alDeleteSources") && a.bind(a.alGenBuffers, "alGenBuffers") &&
		a.bind(a.alDeleteBuffers, "alDeleteBuffers") && a.bind(a.alBufferData, "alBufferData") &&
		a.bind(a.alSourceQueueBuffers, "alSourceQueueBuffers") &&
		a.bind(a.alSourceUnqueueBuffers, "alSourceUnqueueBuffers") && a.bind(a.alGetSourcei, "alGetSourcei") &&
		a.bind(a.alSourcePlay, "alSourcePlay") && a.bind(a.alSourceStop, "alSourceStop")))
	{
		problem_ = "the OpenAL found is missing a function the sound needs";
		return;
	}
	a.device = a.alcOpenDevice(nullptr);
	if (a.device == nullptr)
	{
		problem_ = "no sound device";
		return;
	}
	a.context = a.alcCreateContext(a.device, nullptr);
	if (a.context == nullptr || a.alcMakeContextCurrent(a.context) == 0)
	{
		problem_ = "OpenAL would not start on the sound device";
		if (a.context != nullptr) a.alcDestroyContext(a.context);
		a.alcCloseDevice(a.device);
		return;
	}
	a.alGenSources(1, &a.source);
	a.alGenBuffers(kBuffers, a.buffers);
	a.free.assign(a.buffers, a.buffers + kBuffers);
	a.started = true;
}

Sound::~Sound()
{
	Api &a = *api_;
	if (!a.started) return;
	a.alSourceStop(a.source);
	a.alDeleteSources(1, &a.source);
	a.alDeleteBuffers(kBuffers, a.buffers);
	a.alcMakeContextCurrent(nullptr);
	a.alcDestroyContext(a.context);
	a.alcCloseDevice(a.device);
	/* OpenAL stays loaded: unloading a library that started threads is not safe */
}

bool Sound::available() const { return api_->started; }

void Sound::scale(const int16_t *samples, int32_t pairs, int32_t volume, std::vector<int16_t> &out)
{
	const int32_t v = volume < 0 ? 0 : volume > 100 ? 100 : volume;
	out.resize(static_cast<size_t>(pairs) * 2);
	for (size_t i = 0; i < out.size(); i++) out[i] = static_cast<int16_t>(static_cast<int32_t>(samples[i]) * v / 100);
}

void Sound::play(const int16_t *samples, int32_t pairs, int32_t volume)
{
	Api &a = *api_;
	if (!a.started || samples == nullptr || pairs <= 0 || volume <= 0) return;
	if (volume < 100)
	{
		scale(samples, pairs, volume, scaled_);
		samples = scaled_.data();
	}

	int done = 0;
	a.alGetSourcei(a.source, AL_BUFFERS_PROCESSED, &done);
	while (done-- > 0)
	{
		unsigned b = 0;
		a.alSourceUnqueueBuffers(a.source, 1, &b);
		a.free.push_back(b);
	}
	int waiting = 0;
	a.alGetSourcei(a.source, AL_BUFFERS_QUEUED, &waiting);
	if (waiting >= kMostAhead || a.free.empty())
	{
		dropped_ += pairs;
		return;
	}
	const unsigned b = a.free.back();
	a.free.pop_back();
	a.alBufferData(b, AL_FORMAT_STEREO16, samples, pairs * 4, kRate);
	a.alSourceQueueBuffers(a.source, 1, &b);
	queued_ += pairs;
	int state = 0;
	a.alGetSourcei(a.source, AL_SOURCE_STATE, &state);
	/* first sound, or the queue ran dry and the source stopped */
	if (state != AL_PLAYING) a.alSourcePlay(a.source);
}

} // namespace chimera::vm

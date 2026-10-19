/* gl_names.h - the GL names a core holds, by kind, as they crossed the GPU
 * bridge.
 *
 * A load that makes a core rebuild its GL objects also gives it a fresh
 * context (gl_bridge.cpp, renewContext), and a fresh context hands out names
 * from 1 again. A restored state can still hold names from the context that
 * went - a texture the core deletes after it rebuilt, a sampler it forgot -
 * and if a new object were given one of those numbers, the stale call would
 * reach the new object: a delete would free it, a bind would draw the wrong
 * thing, with no error anywhere (Ruffle, 2026-09-15). So the bridge keeps
 * what the core holds here, and in the fresh context takes every one of those
 * names again, empty, before the core runs: a stale name names nothing, and a
 * new object never gets one. Apart from the GL calls so a test can check it
 * without a driver.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

/* One kind per namespace. Programs and shaders share one. */
enum CeGlNameKind
{
	kNameTexture, kNameBuffer, kNameFramebuffer, kNameRenderbuffer, kNameSampler,
	kNameQuery, kNameVertexArray, kNamePipeline, kNameTransformFeedback, kNameProgram,
	kNameKinds
};

class CeGlNames
{
public:
	/* names above this are not kept: no core comes near it, and a guest
	 * handing in garbage must not make the bridge allocate gigabytes */
	static constexpr uint32_t kMost = 1u << 24;

	void made(int kind, uint32_t name)
	{
		if (name == 0 || name >= kMost) return;
		std::vector<bool> &v = live_[kind];
		if (name >= v.size()) v.resize((size_t)name + 1 + (name >> 2), false);
		v[name] = true;
		if (name < kept_[kind].size()) kept_[kind][name] = false;
	}
	void gone(int kind, uint32_t name)
	{
		std::vector<bool> &v = live_[kind];
		if (name < v.size()) v[name] = false;
		if (name < kept_[kind].size()) kept_[kind][name] = false;
	}
	/* Every name held now was made in a context that has just gone: from here
	 * each one is kept (an empty name in the fresh context) until the core
	 * deletes it or a new object gets it. */
	void keepAll()
	{
		for (int k = 0; k < kNameKinds; k++) kept_[k] = live_[k];
	}
	/* a name held from before the fresh context, which names nothing */
	bool isKept(int kind, uint32_t name) const
	{
		const std::vector<bool> &v = kept_[kind];
		return name < v.size() && v[name];
	}
	bool isLive(int kind, uint32_t name) const
	{
		const std::vector<bool> &v = live_[kind];
		return name < v.size() && v[name];
	}
	/* the highest name of the kind the core holds, 0 for none */
	uint32_t highest(int kind) const
	{
		const std::vector<bool> &v = live_[kind];
		for (size_t i = v.size(); i-- > 1;)
			if (v[i]) return (uint32_t)i;
		return 0;
	}
	size_t count(int kind) const
	{
		size_t n = 0;
		for (bool b : live_[kind]) n += b ? 1 : 0;
		return n;
	}
	void clear()
	{
		for (std::vector<bool> &v : live_) v.clear();
		for (std::vector<bool> &v : kept_) v.clear();
	}

private:
	std::vector<bool> live_[kNameKinds];
	std::vector<bool> kept_[kNameKinds];
};

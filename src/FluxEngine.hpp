// Tape Jam - Flux playback engine (no Rack dependency, unit-testable).
//
// Model: one master clock "t" (seconds of stem time) always advances. Each lane
// renders from a Voice, a pure function of t. With no effect the voice reads at t.
// An effect voice reads somewhere else (loop, reverse, tape stop...). On release
// the lane crossfades back to the plain voice, which is by definition where the
// stem would have been had the effect never happened (Traktor-style Flux mode).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace tapejam {

static const int NUM_LANES = 6;

enum Fx { FX_NONE = -1, FX_ROLL = 0, FX_REVERSE, FX_STOP, FX_STUTTER, NUM_FX };

static const int NUM_LENGTHS = 8;
// Length knob values in beats (1 beat = quarter note).
static const double LENGTH_BEATS[NUM_LENGTHS] = {1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0};

struct Sample {
	std::vector<int16_t> data; // interleaved stereo
	int64_t frames = 0;
	double sampleRate = 44100.0;
	std::string path;
	std::string name;

	double duration() const {
		return sampleRate > 0 ? frames / sampleRate : 0.0;
	}

	inline void frame(int64_t i, float& l, float& r) const {
		if (i < 0 || i >= frames) {
			l = r = 0.f;
			return;
		}
		const int16_t* p = &data[2 * i];
		l = p[0] * (1.f / 32768.f);
		r = p[1] * (1.f / 32768.f);
	}

	static inline float hermite(float y0, float y1, float y2, float y3, float x) {
		float c1 = 0.5f * (y2 - y0);
		float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
		float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
		return ((c3 * x + c2) * x + c1) * x + y1;
	}

	// Read stereo at stem time t (seconds). Outside the file reads silence.
	inline void read(double t, float& outL, float& outR) const {
		double x = t * sampleRate;
		double fl = std::floor(x);
		int64_t i = (int64_t) fl;
		if (i < -2 || i > frames + 1) {
			outL = outR = 0.f;
			return;
		}
		float f = (float) (x - fl);
		float l0, r0, l1, r1, l2, r2, l3, r3;
		frame(i - 1, l0, r0);
		frame(i, l1, r1);
		frame(i + 1, l2, r2);
		frame(i + 2, l3, r3);
		outL = hermite(l0, l1, l2, l3, f);
		outR = hermite(r0, r1, r2, r3, f);
	}
};

struct Grid {
	double beatLen = 0.5; // stem seconds per beat
	double offset = 0.0;  // stem time of a downbeat
};

struct Voice {
	int fx = FX_NONE;
	double engageT = 0.0;
	double len = 0.5;       // latched effect length (s)
	double loopStart = 0.0; // roll: grid-aligned slice start
	double shift = 0.0;     // time shift, used to keep the pre-seek voice alive during a crossfade
};

struct Tap {
	double pos;
	float gain;
};

inline double posMod(double a, double m) {
	double r = std::fmod(a, m);
	return r < 0 ? r + m : r;
}

// Evaluate a voice at master time t. Fills 1 or 2 taps, returns the count.
inline int evalVoice(const Voice& v, double t, const Grid& g, Tap taps[2]) {
	double tt = t + v.shift;
	switch (v.fx) {
		default:
		case FX_NONE:
			taps[0] = {tt, 1.f};
			return 1;

		case FX_ROLL: {
			// Repeat the grid slice containing the engage point. At the engage instant
			// the read position equals tt exactly, so engaging is seamless.
			double len = v.len;
			double phi = posMod(tt - v.loopStart, len);
			double pos = v.loopStart + phi;
			// Pre-roll crossfade into the loop start to avoid a click at each wrap.
			double F = std::min(0.004, len * 0.25);
			if (phi > len - F) {
				float w = (float) ((phi - (len - F)) / F);
				taps[0] = {pos, 1.f - w};
				taps[1] = {pos - len, w};
				return 2;
			}
			taps[0] = {pos, 1.f};
			return 1;
		}

		case FX_REVERSE:
			taps[0] = {2.0 * v.engageT - tt, 1.f};
			return 1;

		case FX_STOP: {
			// Speed ramps linearly 1 -> 0 over len; position is the integral.
			double D = v.len;
			double tau = std::max(0.0, tt - v.engageT);
			double pos, speed;
			if (tau < D) {
				speed = 1.0 - tau / D;
				pos = v.engageT + tau - tau * tau / (2.0 * D);
			}
			else {
				speed = 0.0;
				pos = v.engageT + D * 0.5;
			}
			taps[0] = {pos, (float) std::min(1.0, speed * 6.0)};
			return 1;
		}

		case FX_STUTTER: {
			// Rhythmic gate on the grid: open for the first half of each period.
			double len = v.len;
			double ph = posMod(tt - g.offset, len) / len;
			double r = std::min(0.002 / len, 0.1);
			float gain;
			if (ph >= 0.5)
				gain = 0.f;
			else if (ph < r)
				gain = (float) (ph / r);
			else if (ph > 0.5 - r)
				gain = (float) ((0.5 - ph) / r);
			else
				gain = 1.f;
			taps[0] = {tt, gain};
			return 1;
		}
	}
}

struct Lane {
	bool held[NUM_FX] = {};
	bool armed[NUM_FX] = {};
	uint64_t stamp[NUM_FX] = {};
	Voice fxVoice[NUM_FX];
	Voice cur, prev;
	uint64_t curStamp = 0;
	uint64_t counter = 0;
	float xf = 1.f;          // crossfade progress prev -> cur
	double lastReadPos = 0.0; // for display
	// Dry path for the MIX control: the plain voice at t. It only needs its own crossfade
	// on seeks (from the pre-seek spot, held in dryPrev).
	Voice dryPrev;
	float dryXf = 1.f;

	int activeFx() const {
		return cur.fx;
	}

	// Feed gate state every sample. lenBeats is latched on the rising edge.
	void setGate(int fx, bool high, double t, const Grid& g, double lenBeats) {
		if (high && !held[fx]) {
			held[fx] = true;
			armed[fx] = true;
			stamp[fx] = ++counter;
			Voice v;
			v.fx = fx;
			v.engageT = t;
			v.len = std::max(1e-3, lenBeats * g.beatLen);
			v.loopStart = g.offset + std::floor((t - g.offset) / v.len) * v.len;
			fxVoice[fx] = v;
			update();
		}
		else if (!high && held[fx]) {
			held[fx] = false;
			update();
		}
	}

	// Most recently engaged, still-held effect wins. None held -> Flux return.
	void update() {
		int best = FX_NONE;
		uint64_t bs = 0;
		for (int i = 0; i < NUM_FX; i++) {
			if (held[i] && armed[i] && stamp[i] > bs) {
				bs = stamp[i];
				best = i;
			}
		}
		if (bs == curStamp)
			return;
		transition(best >= 0 ? fxVoice[best] : Voice());
		curStamp = bs;
	}

	void transition(const Voice& next) {
		prev = cur;
		cur = next;
		xf = 0.f;
	}

	// Drop all effects until their gates are re-triggered.
	void cancelAll() {
		for (int i = 0; i < NUM_FX; i++)
			armed[i] = false;
		if (curStamp != 0 || cur.fx != FX_NONE) {
			transition(Voice());
			curStamp = 0;
		}
	}

	// Master jumped oldT -> newT: crossfade from the old audio to the new spot.
	void seek(double oldT, double newT) {
		for (int i = 0; i < NUM_FX; i++)
			armed[i] = false;
		Voice old = cur;
		old.shift += oldT - newT;
		prev = old;
		cur = Voice();
		curStamp = 0;
		xf = 0.f;
		dryPrev = Voice();
		dryPrev.shift = oldT - newT;
		dryXf = 0.f;
	}

	static inline void accumulate(const Sample* s, const Voice& v, double t, const Grid& g, float gain, float& L, float& R, double* posOut) {
		Tap taps[2];
		int n = evalVoice(v, t, g, taps);
		if (posOut)
			*posOut = taps[0].pos;
		for (int k = 0; k < n; k++) {
			float w = taps[k].gain * gain;
			if (w == 0.f)
				continue;
			float l, r;
			s->read(taps[k].pos, l, r);
			L += l * w;
			R += r * w;
		}
	}

	// dxf: crossfade increment per sample (1 / (fadeSeconds * sampleRate)).
	// mix: 1 = effected only, 0 = only the plain stem playing underneath; linear blend between.
	void render(const Sample* s, double t, const Grid& g, float dxf, float& L, float& R, float mix = 1.f) {
		const float wetXf = xf, dx = dryXf; // crossfade positions this sample renders at
		renderWet(s, t, g, dxf, L, R);
		dryXf = std::min(1.f, dryXf + dxf);
		// No effect and no crossfade in progress: the wet path is the plain voice, nothing to blend.
		if (mix >= 1.f || (cur.fx == FX_NONE && wetXf >= 1.f && dx >= 1.f) || !s || s->frames <= 0)
			return;
		float dL = 0.f, dR = 0.f;
		if (dx < 1.f) {
			accumulate(s, Voice(), t, g, std::sin(dx * 1.5707963f), dL, dR, nullptr);
			accumulate(s, dryPrev, t, g, std::cos(dx * 1.5707963f), dL, dR, nullptr);
		}
		else {
			accumulate(s, Voice(), t, g, 1.f, dL, dR, nullptr);
		}
		L = mix * L + (1.f - mix) * dL;
		R = mix * R + (1.f - mix) * dR;
	}

	void renderWet(const Sample* s, double t, const Grid& g, float dxf, float& L, float& R) {
		L = R = 0.f;
		if (xf < 1.f) {
			float a = std::sin(xf * 1.5707963f);
			float b = std::cos(xf * 1.5707963f);
			if (s && s->frames > 0) {
				accumulate(s, cur, t, g, a, L, R, &lastReadPos);
				accumulate(s, prev, t, g, b, L, R, nullptr);
			}
			xf = std::min(1.f, xf + dxf);
		}
		else if (s && s->frames > 0) {
			accumulate(s, cur, t, g, 1.f, L, R, &lastReadPos);
		}
		else {
			lastReadPos = t;
		}
	}
};

// Measures tempo from a clock input.
struct ClockTracker {
	int64_t count = 0;
	bool seenEdge = false;
	bool high = false;
	double bpm = 120.0;
	bool valid = false;

	void reset() {
		count = 0;
		seenEdge = false;
		high = false;
		valid = false;
	}

	// Returns true on a rising edge.
	bool process(float v, double sr, int ppqn) {
		bool edge = false;
		if (!high && v >= 1.f) {
			high = true;
			edge = true;
		}
		else if (high && v <= 0.1f) {
			high = false;
		}
		count++;
		if (edge) {
			if (seenEdge && count > 0) {
				double b = 60.0 * sr / ((double) count * ppqn);
				b = std::max(20.0, std::min(400.0, b));
				// Jump on big changes, smooth jitter on small ones.
				if (!valid || std::fabs(b - bpm) / bpm > 0.1)
					bpm = b;
				else
					bpm += (b - bpm) * 0.5;
				valid = true;
			}
			seenEdge = true;
			count = 0;
		}
		// Clock stopped for 3 s: fall back to the knob.
		if (count > (int64_t) (3.0 * sr))
			valid = false;
		return edge;
	}
};

} // namespace tapejam

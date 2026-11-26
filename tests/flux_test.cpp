// Offline tests for the Flux engine. Build: see tests/run.sh
#define _USE_MATH_DEFINES
#include <cmath>
#include "FluxEngine.hpp"
#include "WavLoad.hpp"
#include "dr_wav.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace tapejam;

static const double SR = 48000.0;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

// Pure sine: any discontinuity shows up as a large sample-to-sample step.
static Sample makeSine(double seconds, double freq) {
	Sample s;
	s.sampleRate = SR;
	s.frames = (int64_t) (seconds * SR);
	s.data.resize(s.frames * 2);
	for (int64_t i = 0; i < s.frames; i++) {
		double v = 0.5 * std::sin(2 * M_PI * freq * i / SR);
		s.data[2 * i] = s.data[2 * i + 1] = (int16_t) (v * 32767);
	}
	return s;
}

// Rhythmic test loop at 120 BPM so effects are audible.
static Sample makeGroove(double seconds) {
	Sample s;
	s.sampleRate = 44100.0; // deliberately != engine rate
	s.frames = (int64_t) (seconds * s.sampleRate);
	s.data.resize(s.frames * 2);
	const double beat = 0.5;
	const double bassNotes[4] = {55.0, 65.41, 49.0, 73.42};
	uint32_t rng = 1;
	for (int64_t i = 0; i < s.frames; i++) {
		double t = i / s.sampleRate;
		double tb = std::fmod(t, beat);
		double t8 = std::fmod(t, beat / 2);
		double kick = std::sin(2 * M_PI * (50 + 120 * std::exp(-tb * 30)) * tb) * std::exp(-tb * 8);
		rng = rng * 1664525u + 1013904223u;
		double noise = ((rng >> 9) / 4194304.0) - 1.0;
		double hat = noise * std::exp(-t8 * 60) * 0.25;
		int bar = (int) (t / (beat * 4)) % 4;
		double bass = 0.3 * std::sin(2 * M_PI * bassNotes[bar] * t);
		double lead = 0.15 * std::sin(2 * M_PI * (440 + 220 * std::fmod(t, 2.0)) * t);
		double l = 0.6 * kick + hat + bass + lead;
		double r = 0.6 * kick + hat * 0.6 + bass + lead * 0.5;
		s.data[2 * i] = (int16_t) (std::max(-1.0, std::min(1.0, l * 0.7)) * 32767);
		s.data[2 * i + 1] = (int16_t) (std::max(-1.0, std::min(1.0, r * 0.7)) * 32767);
	}
	return s;
}

struct Event {
	double time;
	std::function<void(Lane&, double& t)> fn;
};

// Render 'secs' of a lane driven by events; also renders an untouched reference.
static void run(const Sample& s, double secs, std::vector<Event> events, std::vector<float>& out, std::vector<float>& ref, std::vector<int>& fxTrace) {
	Grid g;
	g.beatLen = 0.5;
	Lane lane, refLane;
	bool gate[NUM_FX] = {};
	double lenBeats = 0.5;
	double t = 0;
	float dxf = 1.f / (0.008f * SR);
	size_t ev = 0;
	int64_t n = (int64_t) (secs * SR);
	out.assign(n * 2, 0.f);
	ref.assign(n * 2, 0.f);
	fxTrace.assign(n, FX_NONE);
	(void) gate;
	(void) lenBeats;
	for (int64_t i = 0; i < n; i++) {
		while (ev < events.size() && events[ev].time <= i / SR) {
			events[ev].fn(lane, t);
			ev++;
		}
		float l, r;
		lane.render(&s, t, g, dxf, l, r);
		out[2 * i] = l;
		out[2 * i + 1] = r;
		refLane.render(&s, t, g, dxf, l, r);
		ref[2 * i] = l;
		ref[2 * i + 1] = r;
		fxTrace[i] = lane.activeFx();
		t += 1.0 / SR;
	}
}

static Grid G() {
	Grid g;
	g.beatLen = 0.5;
	return g;
}

static Event gateEv(double time, int fx, bool high, double lenBeats = 0.5) {
	return {time, [=](Lane& l, double& t) { l.setGate(fx, high, t, G(), lenBeats); }};
}

static float maxStep(const std::vector<float>& x, int64_t from, int64_t to) {
	float m = 0;
	for (int64_t i = std::max<int64_t>(from, 1); i < to; i++)
		m = std::max(m, std::fabs(x[2 * i] - x[2 * (i - 1)]));
	return m;
}

static float maxDiff(const std::vector<float>& a, const std::vector<float>& b, int64_t from, int64_t to) {
	float m = 0;
	for (int64_t i = from; i < to; i++)
		m = std::max(m, std::fabs(a[2 * i] - b[2 * i]));
	return m;
}

static void writeWav(const char* path, const std::vector<float>& st) {
	drwav_data_format fmt = {};
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
	fmt.channels = 2;
	fmt.sampleRate = (drwav_uint32) SR;
	fmt.bitsPerSample = 32;
	drwav w;
	if (!drwav_init_file_write(&w, path, &fmt, NULL)) {
		std::printf("  could not write %s\n", path);
		return;
	}
	drwav_write_pcm_frames(&w, st.size() / 2, st.data());
	drwav_uninit(&w);
}

static const char* FX_NAMES[NUM_FX] = {"roll", "reverse", "stop", "stutter"};

int main() {
	Sample sine = makeSine(6.0, 217.0); // not an integer number of cycles per loop
	// Natural step of a 0.5-amplitude 220 Hz sine is ~0.0144/sample.
	const float CLICK = 0.05f;

	// 1. Each effect: engage, release, verify Flux return and no clicks.
	for (int fx = 0; fx < NUM_FX; fx++) {
		std::printf("[%s] flux return\n", FX_NAMES[fx]);
		std::vector<float> out, ref;
		std::vector<int> tr;
		run(sine, 4.0, {gateEv(1.13, fx, true), gateEv(2.37, fx, false)}, out, ref, tr);
		int64_t on = (int64_t) (1.13 * SR), off = (int64_t) (2.37 * SR);
		int64_t settled = off + (int64_t) (0.009 * SR);
		float step = maxStep(out, 0, out.size() / 2);
		float during = maxDiff(out, ref, on + (int64_t) (0.05 * SR), off);
		float after = maxDiff(out, ref, settled, out.size() / 2);
		std::printf("  max step %.4f, diff during %.4f, diff after release %.6f\n", step, during, after);
		CHECK(step < CLICK, "click detected (step %.4f)", step);
		CHECK(during > 0.05f, "effect had no audible effect");
		CHECK(after == 0.f, "did not return to the flux position (diff %.6f)", after);
		CHECK(tr[on + 10] == fx && tr[off + 10] == FX_NONE, "active-fx trace wrong");
	}

	// 2. Priority: roll held, reverse pressed on top, reverse released -> back to roll, roll released -> flux.
	{
		std::printf("[priority] most recent held gate wins\n");
		std::vector<float> out, ref;
		std::vector<int> tr;
		run(sine, 4.0, {gateEv(1.0, FX_ROLL, true), gateEv(1.5, FX_REVERSE, true), gateEv(2.0, FX_REVERSE, false), gateEv(2.5, FX_ROLL, false)}, out, ref, tr);
		CHECK(tr[(int64_t) (1.2 * SR)] == FX_ROLL, "expected roll");
		CHECK(tr[(int64_t) (1.7 * SR)] == FX_REVERSE, "expected reverse");
		CHECK(tr[(int64_t) (2.2 * SR)] == FX_ROLL, "expected fallback to roll");
		CHECK(tr[(int64_t) (2.7 * SR)] == FX_NONE, "expected none");
		float after = maxDiff(out, ref, (int64_t) (2.51 * SR), out.size() / 2);
		float step = maxStep(out, 0, out.size() / 2);
		std::printf("  max step %.4f, diff after release %.6f\n", step, after);
		CHECK(after == 0.f, "no flux return after stacked effects");
		CHECK(step < CLICK, "click detected (step %.4f)", step);
	}

	// 3. Seek cancels effects and lands exactly; held gate does not re-engage until re-triggered.
	{
		std::printf("[seek] cancels effects, crossfades\n");
		Sample groove = makeSine(10.0, 220.0);
		std::vector<float> out, ref;
		std::vector<int> tr;
		run(groove, 3.0, {gateEv(0.5, FX_ROLL, true), {1.0, [](Lane& l, double& t) { l.seek(t, 5.0); t = 5.0; }}}, out, ref, tr);
		CHECK(tr[(int64_t) (0.7 * SR)] == FX_ROLL, "expected roll before seek");
		CHECK(tr[(int64_t) (1.5 * SR)] == FX_NONE, "seek should cancel the roll even though its gate is held");
		float step = maxStep(out, 0, out.size() / 2);
		std::printf("  max step %.4f\n", step);
		CHECK(step < CLICK, "click on seek (step %.4f)", step);
	}

	// 4. Roll length is grid-quantized: the loop repeats with period == len.
	{
		std::printf("[roll] period matches grid length\n");
		std::vector<float> out, ref;
		std::vector<int> tr;
		run(sine, 3.0, {gateEv(1.0, FX_ROLL, true, 0.25)}, out, ref, tr); // 1/16 note = 0.125 s
		int64_t p = (int64_t) (0.125 * SR);
		int64_t a = (int64_t) (1.3 * SR);
		float d = 0;
		for (int64_t i = a; i < a + 2000; i++)
			d = std::max(d, std::fabs(out[2 * i] - out[2 * (i + p)]));
		std::printf("  periodicity error %.6f\n", d);
		CHECK(d < 1e-4f, "roll not periodic at 1/16");
	}

	// 5. WAV decode round trip.
	{
		std::printf("[wav] decode round trip\n");
		Sample groove = makeGroove(16.0);
		drwav_data_format fmt = {};
		fmt.container = drwav_container_riff;
		fmt.format = DR_WAVE_FORMAT_PCM;
		fmt.channels = 2;
		fmt.sampleRate = 44100;
		fmt.bitsPerSample = 16;
		drwav w;
		drwav_init_file_write(&w, "tests/out/groove_src.wav", &fmt, NULL);
		drwav_write_pcm_frames(&w, groove.frames, groove.data.data());
		drwav_uninit(&w);
		FILE* f = std::fopen("tests/out/groove_src.wav", "rb");
		std::fseek(f, 0, SEEK_END);
		long sz = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		std::vector<uint8_t> bytes(sz);
		std::fread(bytes.data(), 1, sz, f);
		std::fclose(f);
		std::string err;
		Sample* s = decodeWav(bytes.data(), bytes.size(), &err);
		CHECK(s && s->frames == groove.frames && s->sampleRate == 44100.0 && s->data == groove.data, "round trip mismatch");

		// 6. Listening demos: each effect on the groove, engaged on beat 9 for 2 beats.
		if (s) {
			for (int fx = 0; fx < NUM_FX; fx++) {
				std::vector<float> out, ref;
				std::vector<int> tr;
				std::vector<Event> evs;
				// Three hits: 1 beat, 2 beats, 1 beat, at 1/16 length (0.25 beat) for roll/stutter, 1 beat for stop.
				double lb = fx == FX_STOP ? 1.0 : 0.25;
				double hits[3][2] = {{4.0, 4.5}, {6.0, 7.0}, {9.75, 10.25}};
				for (auto& h : hits) {
					evs.push_back(gateEv(h[0], fx, true, lb));
					evs.push_back(gateEv(h[1], fx, false, lb));
				}
				run(*s, 12.0, evs, out, ref, tr);
				char path[256];
				std::snprintf(path, sizeof(path), "tests/out/demo_%s.wav", FX_NAMES[fx]);
				writeWav(path, out);
			}
			std::vector<float> out, ref;
			std::vector<int> tr;
			run(*s, 12.0, {}, out, ref, tr);
			writeWav("tests/out/demo_dry.wav", ref);
		}
		delete s;
	}

	std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
	return failures ? 1 : 0;
}

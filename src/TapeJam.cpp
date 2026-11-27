#include "plugin.hpp"
#include "FluxEngine.hpp"
#include "Layout.hpp"
#include "WavLoad.hpp"
#include <osdialog.h>
#include <algorithm>
#include <atomic>
#include <thread>

using namespace tapejam;

static const char* const FX_LABELS[NUM_FX] = {"Loop roll", "Reverse", "Tape stop", "Stutter"};
static const char* const FX_SHORT[NUM_FX] = {"ROLL", "REV", "STOP", "STUT"};
static const std::vector<std::string> LENGTH_LABELS = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"};
static const int PPQN_VALUES[] = {1, 2, 4, 8, 24};

static double dclamp(double x, double lo, double hi) {
	return std::max(lo, std::min(hi, x));
}

static NVGcolor fxColor(int fx) {
	switch (fx) {
		case FX_ROLL: return nvgRGB(0x34, 0xd2, 0xff);
		case FX_REVERSE: return nvgRGB(0xff, 0x52, 0xd9);
		case FX_STOP: return nvgRGB(0xff, 0x9a, 0x2e);
		case FX_STUTTER: return nvgRGB(0xf5, 0xe1, 0x4a);
		default: return nvgRGB(0xe8, 0xe8, 0xe8);
	}
}

template <int FX>
struct FxLight : GrayModuleLightWidget {
	FxLight() {
		addBaseColor(fxColor(FX));
	}
};

struct TapeJam : Module {
	enum ParamId {
		ENUMS(FX_PARAM, NUM_LANES * NUM_FX),
		ENUMS(LEN_PARAM, NUM_LANES),
		ENUMS(LEVEL_PARAM, NUM_LANES),
		ENUMS(MUTE_PARAM, NUM_LANES),
		PLAY_PARAM,
		RESET_PARAM,
		BPM_PARAM,
		OFFSET_PARAM,
		SYNC_PARAM,
		// Appended so saved patches keep their settings.
		ENUMS(MIX_PARAM, NUM_LANES),
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(FX_INPUT, NUM_LANES * NUM_FX),
		CLOCK_INPUT,
		PLAY_INPUT,
		RESET_INPUT,
		SEEK_INPUT,
		SEEK_TRIG_INPUT,
		// Appended after the original inputs so saved patches keep their cables.
		ENUMS(LEN_INPUT, NUM_LANES),
		ENUMS(LEVEL_INPUT, NUM_LANES),
		ENUMS(MUTE_INPUT, NUM_LANES),
		ENUMS(MIX_INPUT, NUM_LANES),
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(L_OUTPUT, NUM_LANES),
		ENUMS(R_OUTPUT, NUM_LANES),
		POS_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(FX_LIGHT, NUM_LANES * NUM_FX),
		ENUMS(MUTE_LIGHT, NUM_LANES),
		PLAY_LIGHT,
		SYNC_LIGHT,
		LIGHTS_LEN
	};

	enum LoadState { LOAD_IDLE, LOAD_BUSY, LOAD_ERROR };

	// Sample hand-off: loader thread -> pending -> (audio thread) current -> garbage -> (UI thread) delete.
	// The audio thread never allocates or frees sample memory.
	std::atomic<Sample*> current[NUM_LANES];
	std::atomic<Sample*> pending[NUM_LANES];
	std::atomic<Sample*> garbage[NUM_LANES];
	std::atomic<int> loadState[NUM_LANES];
	std::thread loaders[NUM_LANES];
	std::string paths[NUM_LANES]; // UI thread only

	Lane lanes[NUM_LANES];
	dsp::SchmittTrigger gateTrig[NUM_LANES][NUM_FX];
	dsp::SchmittTrigger muteTrig[NUM_LANES];
	bool laneMuted[NUM_LANES] = {};
	dsp::BooleanTrigger playBtn, resetBtn;
	dsp::SchmittTrigger playIn, resetIn, seekIn;
	ClockTracker clock;
	int ppqn = 1;
	dsp::ClockDivider lightDivider;

	double t = 0.0; // master (Flux) position, seconds of stem time
	bool playing = false;
	bool loop = false; // at the end of the longest stem: wrap to 0 instead of stopping
	float runGain = 0.f;
	double speed = 1.0;
	float laneGain[NUM_LANES] = {};
	float laneMix[NUM_LANES];

	// Oscilloscope of the summed lane outputs (mono, volts), box-filtered down to about SCOPE_RATE and kept
	// in a ring the UI reads. Benign races like the other disp values; scopeHead counts points written.
	static const int SCOPE_LEN = 2048;
	static constexpr float SCOPE_RATE = 12000.f;
	float scope[SCOPE_LEN] = {};
	std::atomic<int> scopeHead{0};
	float scopeAcc = 0.f;
	int scopeCount = 0;
	Grid grid;

	// Read by the UI (benign races on plain values).
	double dispT = 0.0, dispMax = 0.0, dispBpm = 120.0, dispSpeed = 1.0;
	bool dispClock = false, dispSync = false, dispLoop = false;
	double dispRead[NUM_LANES] = {};
	int dispFx[NUM_LANES] = {};

	TapeJam() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < NUM_LANES; i++) {
			for (int f = 0; f < NUM_FX; f++) {
				int k = i * NUM_FX + f;
				configButton(FX_PARAM + k, string::f("Lane %d %s", i + 1, FX_LABELS[f]));
				configInput(FX_INPUT + k, string::f("Lane %d %s gate", i + 1, FX_LABELS[f]));
			}
			configSwitch(LEN_PARAM + i, 0.f, NUM_LENGTHS - 1, 4.f, string::f("Lane %d effect length", i + 1), LENGTH_LABELS);
			configParam(LEVEL_PARAM + i, 0.f, 1.f, 1.f, string::f("Lane %d level", i + 1), "%", 0.f, 100.f);
			configSwitch(MUTE_PARAM + i, 0.f, 1.f, 0.f, string::f("Lane %d mute", i + 1), {"Unmuted", "Muted"});
			configInput(LEN_INPUT + i, string::f("Lane %d effect length CV (0-10 V across all lengths, replaces the knob)", i + 1));
			configInput(LEVEL_INPUT + i, string::f("Lane %d level CV (0-10 V, replaces the knob)", i + 1));
			configInput(MUTE_INPUT + i, string::f("Lane %d mute gate (muted while high)", i + 1));
			configParam(MIX_PARAM + i, 0.f, 1.f, 1.f, string::f("Lane %d effect mix (0 = plain stem only, 100 = effect only)", i + 1), "%", 0.f, 100.f);
			configInput(MIX_INPUT + i, string::f("Lane %d effect mix CV (0-10 V, replaces the knob)", i + 1));
			laneMix[i] = 1.f;
			configOutput(L_OUTPUT + i, string::f("Lane %d left (mono sum if right is unpatched)", i + 1));
			configOutput(R_OUTPUT + i, string::f("Lane %d right", i + 1));
			current[i].store(nullptr);
			pending[i].store(nullptr);
			garbage[i].store(nullptr);
			loadState[i].store(LOAD_IDLE);
		}
		configButton(PLAY_PARAM, "Play / stop");
		configButton(RESET_PARAM, "Reset to start");
		configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Tempo (stems' BPM)", " BPM");
		configParam(OFFSET_PARAM, 0.f, 2.f, 0.f, "Beat grid offset (first downbeat)", " ms", 0.f, 1000.f);
		configSwitch(SYNC_PARAM, 0.f, 1.f, 0.f, "Clock mode", {"Clock sets the effect grid", "Clock varispeeds the stems"});
		configInput(CLOCK_INPUT, "Clock");
		configInput(PLAY_INPUT, "Play / stop trigger");
		configInput(RESET_INPUT, "Reset trigger");
		configInput(SEEK_INPUT, "Seek position (0-10 V)");
		configInput(SEEK_TRIG_INPUT, "Seek trigger");
		configOutput(POS_OUTPUT, "Position (0-10 V across the longest stem)");
		lightDivider.setDivision(64);
	}

	~TapeJam() {
		for (int i = 0; i < NUM_LANES; i++) {
			if (loaders[i].joinable())
				loaders[i].join();
			delete current[i].exchange(nullptr);
			delete pending[i].exchange(nullptr);
			delete garbage[i].exchange(nullptr);
		}
	}

	// UI thread. Empty path clears the lane.
	void loadLane(int i, const std::string& path) {
		if (loaders[i].joinable())
			loaders[i].join();
		paths[i] = path;
		if (path.empty()) {
			delete pending[i].exchange(new Sample);
			loadState[i] = LOAD_IDLE;
			return;
		}
		loadState[i] = LOAD_BUSY;
		loaders[i] = std::thread([this, i, path]() {
			Sample* s = nullptr;
			std::string err;
			try {
				std::vector<uint8_t> bytes = system::readFile(path);
				if (!bytes.empty())
					s = decodeWav(bytes.data(), bytes.size(), &err);
			}
			catch (...) {
			}
			if (!s) {
				WARN("Ljomi Tracker: could not load %s", path.c_str());
				loadState[i] = LOAD_ERROR;
				return;
			}
			s->path = path;
			s->name = system::getFilename(path);
			delete pending[i].exchange(s);
			loadState[i] = LOAD_IDLE;
		});
	}

	// UI thread: free samples the audio thread has swapped out.
	void collectGarbage() {
		for (int i = 0; i < NUM_LANES; i++)
			delete garbage[i].exchange(nullptr);
	}

	void seekTo(double newT) {
		for (int i = 0; i < NUM_LANES; i++)
			lanes[i].seek(t, newT);
		t = newT;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < NUM_LANES; i++)
			loadLane(i, "");
		seekTo(0.0);
		playing = false;
		loop = false;
		ppqn = 1;
	}

	void process(const ProcessArgs& args) override {
		const double sr = args.sampleRate;

		// Take newly loaded samples (only when the previous one has been collected).
		Sample* smp[NUM_LANES];
		double maxDur = 0.0;
		for (int i = 0; i < NUM_LANES; i++) {
			if (!garbage[i].load(std::memory_order_acquire)) {
				Sample* p = pending[i].exchange(nullptr, std::memory_order_acq_rel);
				if (p) {
					garbage[i].store(current[i].load(std::memory_order_relaxed), std::memory_order_release);
					current[i].store(p, std::memory_order_release);
				}
			}
			smp[i] = current[i].load(std::memory_order_relaxed);
			if (smp[i])
				maxDur = std::max(maxDur, smp[i]->duration());
		}

		// Tempo
		bool clockPatched = inputs[CLOCK_INPUT].isConnected();
		if (clockPatched)
			clock.process(inputs[CLOCK_INPUT].getVoltage(), sr, ppqn);
		else if (clock.seenEdge)
			clock.reset();
		bool sync = params[SYNC_PARAM].getValue() > 0.5f;
		double knobBpm = params[BPM_PARAM].getValue();
		double bpm = knobBpm, targetSpeed = 1.0;
		if (clockPatched && clock.valid) {
			if (sync)
				targetSpeed = clock.bpm / knobBpm;
			else
				bpm = clock.bpm;
		}
		speed += (targetSpeed - speed) * std::min(1.0, 1.0 / (0.03 * sr));
		grid.beatLen = 60.0 / bpm;
		grid.offset = params[OFFSET_PARAM].getValue();

		// Transport
		bool playPressed = playBtn.process(params[PLAY_PARAM].getValue() > 0.f);
		if (playIn.process(inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f))
			playPressed = true;
		if (playPressed) {
			playing = !playing;
			if (playing && maxDur > 0.0 && t >= maxDur)
				seekTo(0.0);
		}
		bool rst = resetBtn.process(params[RESET_PARAM].getValue() > 0.f);
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			rst = true;
		if (rst)
			seekTo(0.0);
		if (seekIn.process(inputs[SEEK_TRIG_INPUT].getVoltage(), 0.1f, 1.f))
			seekTo(clamp(inputs[SEEK_INPUT].getVoltage() / 10.f, 0.f, 1.f) * maxDur);

		const float dxf = 1.f / (0.008f * sr);
		const float ramp = 1.f / (0.005f * sr);
		runGain = playing ? std::min(1.f, runGain + ramp) : std::max(0.f, runGain - ramp);

		// Lanes
		float mixSum = 0.f;
		for (int i = 0; i < NUM_LANES; i++) {
			int lenIdx = inputs[LEN_INPUT + i].isConnected()
				? (int) std::round(clamp(inputs[LEN_INPUT + i].getVoltage(), 0.f, 10.f) / 10.f * (NUM_LENGTHS - 1))
				: clamp((int) std::round(params[LEN_PARAM + i].getValue()), 0, NUM_LENGTHS - 1);
			double lenBeats = LENGTH_BEATS[lenIdx];
			for (int f = 0; f < NUM_FX; f++) {
				int k = i * NUM_FX + f;
				gateTrig[i][f].process(inputs[FX_INPUT + k].getVoltage(), 0.1f, 1.f);
				bool high = gateTrig[i][f].isHigh() || params[FX_PARAM + k].getValue() > 0.f;
				lanes[i].setGate(f, high, t, grid, lenBeats);
			}
			float mixTarget = inputs[MIX_INPUT + i].isConnected()
				? clamp(inputs[MIX_INPUT + i].getVoltage() / 10.f, 0.f, 1.f)
				: params[MIX_PARAM + i].getValue();
			laneMix[i] += clamp(mixTarget - laneMix[i], -ramp, ramp);
			float l, r;
			lanes[i].render(smp[i], t, grid, dxf, l, r, laneMix[i]);

			muteTrig[i].process(inputs[MUTE_INPUT + i].getVoltage(), 0.1f, 1.f);
			laneMuted[i] = params[MUTE_PARAM + i].getValue() > 0.5f || (inputs[MUTE_INPUT + i].isConnected() && muteTrig[i].isHigh());
			float level = inputs[LEVEL_INPUT + i].isConnected()
				? clamp(inputs[LEVEL_INPUT + i].getVoltage() / 10.f, 0.f, 1.f)
				: params[LEVEL_PARAM + i].getValue();
			float target = laneMuted[i] ? 0.f : level;
			laneGain[i] += clamp(target - laneGain[i], -ramp, ramp);
			float g = laneGain[i] * runGain * 5.f;
			l *= g;
			r *= g;
			if (outputs[L_OUTPUT + i].isConnected() && !outputs[R_OUTPUT + i].isConnected())
				outputs[L_OUTPUT + i].setVoltage(0.5f * (l + r));
			else
				outputs[L_OUTPUT + i].setVoltage(l);
			outputs[R_OUTPUT + i].setVoltage(r);
			mixSum += 0.5f * (l + r);
			dispRead[i] = lanes[i].lastReadPos;
			dispFx[i] = lanes[i].activeFx();
		}

		scopeAcc += mixSum;
		int decim = std::max(1, (int) std::round(sr / SCOPE_RATE));
		if (++scopeCount >= decim) {
			int h = scopeHead.load(std::memory_order_relaxed);
			scope[h % SCOPE_LEN] = scopeAcc / scopeCount;
			scopeHead.store(h + 1, std::memory_order_release);
			scopeAcc = 0.f;
			scopeCount = 0;
		}

		outputs[POS_OUTPUT].setVoltage(maxDur > 0.0 ? (float) (10.0 * dclamp(t / maxDur, 0.0, 1.0)) : 0.f);

		if (playing || runGain > 0.f)
			t += speed / sr;
		if (playing && maxDur > 0.0 && t >= maxDur) {
			if (loop)
				seekTo(std::fmod(t, maxDur)); // keeps the overshoot so the loop length is exact
			else
				playing = false;
		}

		dispT = t;
		dispMax = maxDur;
		dispBpm = bpm;
		dispSpeed = speed;
		dispClock = clockPatched && clock.valid;
		dispSync = sync;
		dispLoop = loop;

		if (lightDivider.process()) {
			for (int i = 0; i < NUM_LANES; i++) {
				for (int f = 0; f < NUM_FX; f++) {
					int k = i * NUM_FX + f;
					float b = lanes[i].activeFx() == f ? 1.f : (lanes[i].held[f] ? 0.15f : 0.f);
					lights[FX_LIGHT + k].setBrightness(b);
				}
				lights[MUTE_LIGHT + i].setBrightness(laneMuted[i] ? 1.f : 0.f);
			}
			lights[PLAY_LIGHT].setBrightness(playing ? 1.f : 0.f);
			lights[SYNC_LIGHT].setBrightness(sync ? 1.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		for (int i = 0; i < NUM_LANES; i++)
			json_array_append_new(arr, json_string(paths[i].c_str()));
		json_object_set_new(root, "lanes", arr);
		json_object_set_new(root, "ppqn", json_integer(ppqn));
		json_object_set_new(root, "loop", json_boolean(loop));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* p = json_object_get(root, "ppqn");
		if (p)
			ppqn = std::max(1, (int) json_integer_value(p));
		json_t* lp = json_object_get(root, "loop");
		if (lp)
			loop = json_boolean_value(lp);
		json_t* arr = json_object_get(root, "lanes");
		if (arr) {
			for (int i = 0; i < NUM_LANES && i < (int) json_array_size(arr); i++) {
				const char* s = json_string_value(json_array_get(arr, i));
				if (s && *s)
					loadLane(i, s);
			}
		}
	}
};

// ---------------------------------------------------------------- UI

static std::string lastDir;

static void openLoadDialog(TapeJam* m, int lane) {
	osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
	char* p = osdialog_file(OSDIALOG_OPEN, lastDir.empty() ? NULL : lastDir.c_str(), NULL, filters);
	osdialog_filters_free(filters);
	if (!p)
		return;
	std::string path = p;
	std::free(p);
	lastDir = system::getDirectory(path);
	m->loadLane(lane, path);
}

static void openFolderDialog(TapeJam* m) {
	char* p = osdialog_file(OSDIALOG_OPEN_DIR, lastDir.empty() ? NULL : lastDir.c_str(), NULL, NULL);
	if (!p)
		return;
	std::string dir = p;
	std::free(p);
	lastDir = dir;
	std::vector<std::string> wavs;
	for (const std::string& e : system::getEntries(dir)) {
		std::string ext = string::lowercase(system::getExtension(e));
		if (ext == ".wav" || ext == "wav")
			wavs.push_back(e);
	}
	std::sort(wavs.begin(), wavs.end());
	for (int i = 0; i < NUM_LANES && i < (int) wavs.size(); i++)
		m->loadLane(i, wavs[i]);
}

static std::string fmtTime(double s) {
	if (s < 0)
		s = 0;
	int m = (int) (s / 60);
	return string::f("%d:%04.1f", m, s - m * 60);
}

// Fonts match ljomi-systems.com: Unbounded Light for the title, Hanken Grotesk for everything else (both OFL,
// static instances cut from the Google Fonts variable fonts; licences in res/fonts).
static std::shared_ptr<window::Font> displayFont() {
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Unbounded-Light.ttf"));
}

static std::shared_ptr<window::Font> textFont() {
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/HankenGrotesk-Medium.ttf"));
}

static float textWidth(NVGcontext* vg, const std::string& s) {
	float b[4];
	return nvgTextBounds(vg, 0, 0, s.c_str(), NULL, b);
}

struct LaneDisplay : Widget {
	TapeJam* module = nullptr;
	int lane = 0;
	// Marquee for names wider than the screen: the scroll restarts whenever the name changes.
	std::string scrollName;
	double scrollStart = 0.0;

	// Scroll offset (px) for text overflowing by `over` px: hold, scroll to the end, hold, jump back.
	float marqueeOffset(const std::string& name, float over) {
		const double hold = 1.5, pxPerSec = 15.0;
		double now = system::getTime();
		if (name != scrollName) {
			scrollName = name;
			scrollStart = now;
		}
		double travel = over / pxPerSec;
		double ph = std::fmod(now - scrollStart, hold + travel + hold);
		return (float) (dclamp(ph - hold, 0.0, travel) * pxPerSec);
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, nvgRGB(0x08, 0x0f, 0x1e));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x24, 0x44, 0x5a));
		nvgStrokeWidth(args.vg, 0.7f);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return Widget::drawLayer(args, layer);
		auto font = textFont();
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 10.5f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

		const float pad = 4.f;
		const float barY = box.size.y - 9.f, barH = 5.f, barW = box.size.x - 2 * pad;

		std::string name = string::f("LANE %d", lane + 1);
		std::string right;
		NVGcolor nameCol = nvgRGB(0x64, 0x8d, 0xa6);
		double dur = 0, master = 0, readPos = 0;
		int fx = FX_NONE;
		if (module) {
			Sample* s = module->current[lane].load();
			int st = module->loadState[lane].load();
			master = module->dispT;
			readPos = module->dispRead[lane];
			fx = module->dispFx[lane];
			if (st == TapeJam::LOAD_BUSY) {
				name = "loading...";
			}
			else if (st == TapeJam::LOAD_ERROR) {
				name = "load failed";
				nameCol = nvgRGB(0xff, 0x55, 0x55);
			}
			else if (s && s->frames > 0) {
				name = s->name;
				nameCol = nvgRGB(0xdc, 0xeb, 0xf2);
				dur = s->duration();
				right = fmtTime(dur);
			}
			else {
				name = "click to load";
			}
		}
		// Name first; the duration only shows when both fit.
		const float avail = box.size.x - 2 * pad;
		if (!right.empty() && textWidth(args.vg, name) + 4.f + textWidth(args.vg, right) > avail)
			right.clear();
		float over = textWidth(args.vg, name) - avail;
		nvgFillColor(args.vg, nameCol);
		if (over > 0.f) {
			float off = marqueeOffset(name, over);
			nvgSave(args.vg);
			nvgIntersectScissor(args.vg, pad, 0.f, avail, barY - 1.5f);
			nvgText(args.vg, pad - off, 3.f, name.c_str(), NULL);
			nvgRestore(args.vg);
		}
		else {
			nvgText(args.vg, pad, 3.f, name.c_str(), NULL);
		}
		if (!right.empty()) {
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, nvgRGB(0x64, 0x8d, 0xa6));
			nvgText(args.vg, box.size.x - pad, 3.f, right.c_str(), NULL);
		}

		// Timeline bar
		nvgBeginPath(args.vg);
		nvgRect(args.vg, pad, barY, barW, barH);
		nvgFillColor(args.vg, nvgRGB(0x14, 0x23, 0x3a));
		nvgFill(args.vg);
		if (dur > 0) {
			float mx = pad + barW * (float) dclamp(master / dur, 0.0, 1.0);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, pad, barY, mx - pad, barH);
			nvgFillColor(args.vg, nvgRGB(0x2a, 0x6e, 0x82));
			nvgFill(args.vg);
			if (fx != FX_NONE) {
				// Ghost: where the stem would be (Flux position) vs where it is reading.
				float rx = pad + barW * (float) dclamp(readPos / dur, 0.0, 1.0);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, mx - 0.5f, barY - 1.5f, 1.f, barH + 3.f);
				nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
				nvgFill(args.vg);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, rx - 1.f, barY - 1.5f, 2.f, barH + 3.f);
				nvgFillColor(args.vg, fxColor(fx));
				nvgFill(args.vg);
			}
			else {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, mx - 0.75f, barY - 1.5f, 1.5f, barH + 3.f);
				nvgFillColor(args.vg, nvgRGB(0x9e, 0xe4, 0xaa));
				nvgFill(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			openLoadDialog(module, lane);
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}
};

struct TransportDisplay : Widget {
	TapeJam* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
		nvgFillColor(args.vg, nvgRGB(0x08, 0x0f, 0x1e));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x24, 0x44, 0x5a));
		nvgStrokeWidth(args.vg, 0.7f);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return Widget::drawLayer(args, layer);
		auto font = textFont();
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		double t = module ? module->dispT : 0, mx = module ? module->dispMax : 0;
		double bpm = module ? module->dispBpm : 120;
		std::string line1 = fmtTime(t) + " / " + fmtTime(mx);
		std::string src = !module ? "INT" : module->dispClock ? (module->dispSync ? "VARI" : "CLK") : "INT";
		std::string line2 = string::f("%.1f BPM %s", bpm, src.c_str());
		if (module && module->dispClock && module->dispSync)
			line2 += string::f(" x%.3f", module->dispSpeed);
		if (module && module->dispLoop)
			line2 += " LOOP";
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFontSize(args.vg, 15.f);
		nvgFillColor(args.vg, nvgRGB(0x9e, 0xe4, 0xaa));
		nvgText(args.vg, 5.f, 4.f, line1.c_str(), NULL);
		// Measure with every digit as 0 so the scope edge doesn't jitter as the time changes.
		auto zeroed = [](std::string s) {
			for (char& c : s)
				if (c >= '0' && c <= '9')
					c = '0';
			return s;
		};
		float textEnd = 5.f + textWidth(args.vg, zeroed(line1));
		nvgFontSize(args.vg, 10.5f);
		nvgFillColor(args.vg, nvgRGB(0x8b, 0x9d, 0xb3));
		nvgText(args.vg, 5.f, 21.f, line2.c_str(), NULL);
		textEnd = std::max(textEnd, 5.f + textWidth(args.vg, zeroed(line2)));
		drawScope(args.vg, textEnd + 8.f, 4.f, box.size.x - 5.f, box.size.y - 4.f);
		Widget::drawLayer(args, layer);
	}

	// Oscilloscope of the summed output: the last ~40 ms redrawn in place each frame, started on a rising
	// zero crossing (when there is one) so steady tones stand still. Full scale is +-10 V.
	void drawScope(NVGcontext* vg, float x0, float y0, float x1, float y1) {
		if (x1 - x0 < 8.f)
			return;
		const float mid = 0.5f * (y0 + y1), half = 0.5f * (y1 - y0);
		nvgBeginPath(vg);
		nvgRect(vg, x0, mid - 0.25f, x1 - x0, 0.5f);
		nvgFillColor(vg, nvgRGB(0x14, 0x23, 0x3a));
		nvgFill(vg);
		if (!module)
			return;
		const int N = TapeJam::SCOPE_LEN;
		const int W = (int) (0.04f * TapeJam::SCOPE_RATE); // points shown
		int head = module->scopeHead.load(std::memory_order_acquire);
		if (head < 2 * W)
			return;
		const float* s = module->scope;
		auto at = [&](int i) { return s[i % N]; };
		// Look back up to one window for a rising crossing; the trace then starts there.
		int start = head - W;
		for (int i = head - W; i > head - 2 * W; i--) {
			if (at(i - 1) < 0.f && at(i) >= 0.f) {
				start = i;
				break;
			}
		}
		auto y = [&](float v) { return mid - clamp(v / 10.f, -1.f, 1.f) * half; };
		float dx = (x1 - x0) / (W - 1);
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y(at(start)));
		for (int k = 1; k < W; k++)
			nvgLineTo(vg, x0 + k * dx, y(at(start + k)));
		nvgStrokeColor(vg, nvgRGB(0x49, 0xc5, 0xa8));
		nvgStrokeWidth(vg, 1.f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
	}
};

// Panel text drawn at runtime (Rack's SVG renderer ignores <text>).
struct PanelLabels : Widget {
	void label(const DrawArgs& args, float xmm, float ymm, const char* text, float size, NVGcolor col, int align = NVG_ALIGN_CENTER) {
		nvgFontSize(args.vg, size);
		nvgFillColor(args.vg, col);
		nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
		Vec p = mm2px(Vec(xmm, ymm));
		nvgText(args.vg, p.x, p.y, text, NULL);
	}

	void draw(const DrawArgs& args) override {
		using namespace layout;
		if (!RUNTIME_LABELS)
			return;
		auto title = displayFont();
		auto font = textFont();
		if (!title || !font)
			return;
		NVGcolor dim = nvgRGB(0x8b, 0x9d, 0xb3);
		nvgFontFaceId(args.vg, title->handle);
		label(args, 5.f, 7.5f, "LJOMI TRACKER", 13.f, nvgRGB(0x9e, 0xe4, 0xaa), NVG_ALIGN_LEFT);
		nvgFontFaceId(args.vg, font->handle);
		label(args, 50.f, 7.5f, "flux stem player", 9.f, dim, NVG_ALIGN_LEFT);
		for (int f = 0; f < NUM_FX; f++)
			label(args, fxJackX(f) + FX_BTN_DX / 2, 12.5f, FX_SHORT[f], 9.f, fxColor(f));
		label(args, LEN_X, 12.5f, "LEN", 9.f, dim);
		label(args, LEN_CV_X, 12.5f, "CV", 9.f, dim);
		label(args, LVL_X, 12.5f, "LVL", 9.f, dim);
		label(args, LVL_CV_X, 12.5f, "CV", 9.f, dim);
		label(args, MIX_X, 12.5f, "MIX", 9.f, dim);
		label(args, MIX_CV_X, 12.5f, "CV", 9.f, dim);
		label(args, MUTE_X, 12.5f, "MUTE", 9.f, dim);
		label(args, MUTE_CV_X, 12.5f, "CV", 9.f, dim);
		label(args, OUTL_X, 12.5f, "L", 9.f, dim);
		label(args, OUTR_X, 12.5f, "R", 9.f, dim);
		for (int i = 0; i < NUM_LANES; i++)
			label(args, 4.f, laneY(i), string::f("%d", i + 1).c_str(), 11.f, dim);
		const float y = TR_LABEL_Y;
		label(args, CLOCK_X, y, "CLOCK", 8.f, dim);
		label(args, BPM_X, y, "BPM", 8.f, dim);
		label(args, OFFSET_X, y, "OFFS", 8.f, dim);
		label(args, SYNC_X, y, "SYNC", 8.f, dim);
		label(args, (PLAY_BTN_X + PLAY_IN_X) / 2, y, "PLAY", 8.f, dim);
		label(args, (RESET_BTN_X + RESET_IN_X) / 2, y, "RESET", 8.f, dim);
		label(args, SEEK_CV_X, y, "SEEK", 8.f, dim);
		label(args, SEEK_TRIG_X, y, "TRIG", 8.f, dim);
		label(args, POS_X, y, "POS", 8.f, dim);
	}
};

template <typename TLight>
static void addFxButton(ModuleWidget* w, Module* m, Vec pos, int paramId, int lightId) {
	w->addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<TLight>>>(pos, m, paramId, lightId));
}

struct TapeJamWidget : ModuleWidget {
	TapeJamWidget(TapeJam* module) {
		using namespace layout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TapeJam.svg")));

		PanelLabels* labels = new PanelLabels;
		labels->box.size = box.size;
		addChild(labels);

		for (int i = 0; i < NUM_LANES; i++) {
			float y = laneY(i);
			LaneDisplay* d = new LaneDisplay;
			d->module = module;
			d->lane = i;
			d->box.pos = mm2px(Vec(DISP_X0, y - DISP_H / 2));
			d->box.size = mm2px(Vec(DISP_W, DISP_H));
			addChild(d);

			for (int f = 0; f < NUM_FX; f++) {
				int k = i * NUM_FX + f;
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(fxJackX(f), y)), module, TapeJam::FX_INPUT + k));
				Vec bp = mm2px(Vec(fxBtnX(f), y));
				switch (f) {
					case FX_ROLL: addFxButton<FxLight<FX_ROLL>>(this, module, bp, TapeJam::FX_PARAM + k, TapeJam::FX_LIGHT + k); break;
					case FX_REVERSE: addFxButton<FxLight<FX_REVERSE>>(this, module, bp, TapeJam::FX_PARAM + k, TapeJam::FX_LIGHT + k); break;
					case FX_STOP: addFxButton<FxLight<FX_STOP>>(this, module, bp, TapeJam::FX_PARAM + k, TapeJam::FX_LIGHT + k); break;
					default: addFxButton<FxLight<FX_STUTTER>>(this, module, bp, TapeJam::FX_PARAM + k, TapeJam::FX_LIGHT + k); break;
				}
			}
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(LEN_X, y)), module, TapeJam::LEN_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(LVL_X, y)), module, TapeJam::LEVEL_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(MIX_X, y)), module, TapeJam::MIX_PARAM + i));
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(Vec(MUTE_X, y)), module, TapeJam::MUTE_PARAM + i, TapeJam::MUTE_LIGHT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(LEN_CV_X, y)), module, TapeJam::LEN_INPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(LVL_CV_X, y)), module, TapeJam::LEVEL_INPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(MIX_CV_X, y)), module, TapeJam::MIX_INPUT + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(MUTE_CV_X, y)), module, TapeJam::MUTE_INPUT + i));
			addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(OUTL_X, y)), module, TapeJam::L_OUTPUT + i));
			addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(OUTR_X, y)), module, TapeJam::R_OUTPUT + i));
		}

		const float y = TR_Y;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CLOCK_X, y)), module, TapeJam::CLOCK_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(BPM_X, y)), module, TapeJam::BPM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(OFFSET_X, y)), module, TapeJam::OFFSET_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(mm2px(Vec(SYNC_X, y)), module, TapeJam::SYNC_PARAM, TapeJam::SYNC_LIGHT));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<GreenLight>>>(mm2px(Vec(PLAY_BTN_X, y)), module, TapeJam::PLAY_PARAM, TapeJam::PLAY_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PLAY_IN_X, y)), module, TapeJam::PLAY_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(RESET_BTN_X, y)), module, TapeJam::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(RESET_IN_X, y)), module, TapeJam::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(SEEK_CV_X, y)), module, TapeJam::SEEK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(SEEK_TRIG_X, y)), module, TapeJam::SEEK_TRIG_INPUT));
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(POS_X, y)), module, TapeJam::POS_OUTPUT));

		TransportDisplay* td = new TransportDisplay;
		td->module = module;
		td->box.pos = mm2px(Vec(TRD_X0, TR_LABEL_Y - 3.f));
		td->box.size = mm2px(Vec(TRD_X1 - TRD_X0, 14.f));
		addChild(td);
	}

	void step() override {
		if (module)
			getModule<TapeJam>()->collectGarbage();
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		TapeJam* m = getModule<TapeJam>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Loop (wrap to start at the end of the longest stem)", "", &m->loop));
		menu->addChild(createMenuItem("Load folder of stems (first 6 WAVs)...", "", [=]() { openFolderDialog(m); }));
		menu->addChild(createMenuItem("Clear all stems", "", [=]() {
			for (int i = 0; i < NUM_LANES; i++)
				m->loadLane(i, "");
		}));
		for (int i = 0; i < NUM_LANES; i++) {
			Sample* s = m->current[i].load();
			std::string name = (s && s->frames > 0) ? s->name : "empty";
			menu->addChild(createSubmenuItem(string::f("Lane %d", i + 1), name, [=](Menu* sub) {
				sub->addChild(createMenuItem("Load WAV...", "", [=]() { openLoadDialog(m, i); }));
				sub->addChild(createMenuItem("Clear", "", [=]() { m->loadLane(i, ""); }));
			}));
		}
		std::vector<std::string> ppqnLabels;
		for (int v : PPQN_VALUES)
			ppqnLabels.push_back(string::f("%d", v));
		menu->addChild(createIndexSubmenuItem("Clock PPQN", ppqnLabels,
			[=]() {
				for (int k = 0; k < (int) LENGTHOF(PPQN_VALUES); k++)
					if (PPQN_VALUES[k] == m->ppqn)
						return (size_t) k;
				return (size_t) 0;
			},
			[=](size_t k) { m->ppqn = PPQN_VALUES[k]; }));
	}
};

Model* modelTapeJam = createModel<TapeJam, TapeJamWidget>("LjomiTracker");

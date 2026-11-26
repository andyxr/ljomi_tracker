#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "WavLoad.hpp"

namespace tapejam {

Sample* decodeWav(const uint8_t* data, size_t size, std::string* err) {
	unsigned int channels = 0, sampleRate = 0;
	drwav_uint64 frames = 0;
	int16_t* pcm = drwav_open_memory_and_read_pcm_frames_s16(data, size, &channels, &sampleRate, &frames, NULL);
	if (!pcm || channels == 0 || sampleRate == 0) {
		if (pcm)
			drwav_free(pcm, NULL);
		if (err)
			*err = "Could not decode WAV";
		return nullptr;
	}
	Sample* s = new Sample;
	s->frames = (int64_t) frames;
	s->sampleRate = sampleRate;
	s->data.resize((size_t) frames * 2);
	for (drwav_uint64 i = 0; i < frames; i++) {
		const int16_t* in = pcm + i * channels;
		s->data[2 * i] = in[0];
		s->data[2 * i + 1] = channels >= 2 ? in[1] : in[0];
	}
	drwav_free(pcm, NULL);
	return s;
}

} // namespace tapejam

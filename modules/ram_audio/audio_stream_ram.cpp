
#include "audio_stream_ram.h"

AudioStreamRAM::AudioStreamRAM() :
		capacity(0),
		nframes(0),
		length(0),
		data(NULL) {
	mix_rate = AudioServer::get_singleton()->get_mix_rate();
}

AudioStreamRAM::~AudioStreamRAM() {
	if (data != NULL) memfree(data);
	data = NULL;
}

int AudioStreamRAM::_resample_from(int source_rate) {
	if (source_rate == 0) return -1;
	if (data == NULL) {
		return -1;
	}

	uint32_t new_length = uint32_t(nframes * (mix_rate / double(source_rate)));
	AudioFrame *new_data = (AudioFrame *)memalloc((new_length) * sizeof(AudioFrame));
	if (new_data == NULL) {
		free(data);
		data = NULL;
		return -2;
	}

	uint64_t mix_offset = 0;

	uint64_t mix_increment = uint64_t((source_rate / double(mix_rate)) * double(FP_LEN));

	for (uint32_t i = 0; i < new_length; ++i) {
		uint32_t idx = 4 + uint32_t(mix_offset >> FP_BITS);
		float mu = (mix_offset & FP_MASK) / float(FP_LEN);
		AudioFrame y0 = ((idx - 3) < nframes) ? data[idx - 3] : AudioFrame(0, 0);
		AudioFrame y1 = ((idx - 2) < nframes) ? data[idx - 2] : AudioFrame(0, 0);
		AudioFrame y2 = ((idx - 1) < nframes) ? data[idx - 1] : AudioFrame(0, 0);
		AudioFrame y3 = ((idx - 0) < nframes) ? data[idx - 0] : AudioFrame(0, 0);

		float mu2 = mu * mu;
		AudioFrame a0 = y3 - y2 - y0 + y1;
		AudioFrame a1 = y0 - y1 - a0;
		AudioFrame a2 = y2 - y0;
		AudioFrame a3 = y1;

		new_data[i] = (a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3);

		mix_offset += mix_increment;
	}

	memfree(data);
	data = new_data;
	nframes = capacity = new_length;
	return nframes;
}

void AudioStreamRAM::update_length() {
	length = (nframes / (float)mix_rate);
}

void AudioStreamRAM::load(String path) {
	ERR_FAIL_COND_MSG(data != NULL, "reloading audio is forbidden");

	if (path.ends_with(".ogg")) {
		_decode_vorbis(path);
	} else if (path.ends_with(".wav")) {
		_decode_wave(path);
	}

	update_length();
}

bool AudioStreamRAM::is_valid() {
	return data != NULL;
}

Ref<AudioStreamPlayback> AudioStreamRAM::instance_playback() {
	Ref<AudioStreamPlaybackRAM> playback;
	playback.instance();
	playback->base = Ref<AudioStreamRAM>(this);
	return playback;
}

String AudioStreamRAM::get_stream_name() const {
	return "RAMAudio";
}

float AudioStreamRAM::get_length() const {
	return length;
}

void AudioStreamRAM::_bind_methods() {
	ClassDB::bind_method("load", &AudioStreamRAM::load);
	ClassDB::bind_method("is_valid", &AudioStreamRAM::is_valid);
}

AudioStreamPlaybackRAM::AudioStreamPlaybackRAM() :
		active(false),
		position(0) {
}

AudioStreamPlaybackRAM::~AudioStreamPlaybackRAM() {
}

void AudioStreamPlaybackRAM::stop() {
	active = false;
}

void AudioStreamPlaybackRAM::start(float p_from_pos) {
	if (base->data == NULL) {
		WARN_PRINT("attempting to play invalid audio");
	}

	seek(p_from_pos);
	active = true;
}

void AudioStreamPlaybackRAM::seek(float p_time) {
	uint32_t max = base->nframes;
	position = p_time * base->mix_rate;
	if (position >= max) {
		position = 0;
	}
}

void AudioStreamPlaybackRAM::mix(AudioFrame *p_buffer, float p_rate, int p_frames) {
	ERR_FAIL_COND(!active);

	uint32_t max = base->data == NULL ? 0 : base->nframes;
	uint32_t end_of_mix = position + p_frames;
	int mix_frames = p_frames;

	if (max <= end_of_mix) {
		mix_frames = p_frames - (end_of_mix - max);
		end_of_mix = max;
		active = false;
	}

	for (int i = 0; i < mix_frames; ++i) {
		p_buffer[i] = base->data[position + i];
	}

	for (int i = mix_frames; i < p_frames; ++i) {
		p_buffer[i] = AudioFrame(0, 0);
	}

	position = end_of_mix;
}

int AudioStreamPlaybackRAM::get_loop_count() const {
	return 0;
}
float AudioStreamPlaybackRAM::get_playback_position() const {
	return position / (float)base->mix_rate;
}
float AudioStreamPlaybackRAM::get_length() const {
	return base->length;
}
bool AudioStreamPlaybackRAM::is_playing() const {
	return active;
}
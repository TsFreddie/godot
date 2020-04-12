
#include "audio_stream_ram.h"
#include "core/os/file_access.h"
#include "thirdparty/misc/stb_vorbis.c"

AudioStreamRAM::AudioStreamRAM() :
		capacity(SAFE_FRAMES), nframes(0), length(0) {
	mix_rate = AudioServer::get_singleton()->get_mix_rate();
	data = (AudioFrame *)memalloc(capacity * sizeof(AudioFrame));
	valid = data != NULL;
}

AudioStreamRAM::~AudioStreamRAM() {
	if (data != NULL) memfree(data);
	data = NULL;
}

int AudioStreamRAM::_decode_vorbis(String filename) {
	if (data == NULL) return -2;

	Error r_error;
	int error;

	FileAccess *file = FileAccess::open(filename, FileAccess::READ, &r_error);
	size_t len = file->get_len();
	uint8_t *file_buf = (uint8_t *)memalloc(len * sizeof(uint8_t));
	file->get_buffer(file_buf, len);
	file->close();

	stb_vorbis *f = stb_vorbis_open_memory(file_buf, len, &error, NULL);
	if (f == NULL) return -1;

	int sample_rate = f->sample_rate;
	int channels = f->channels;

	float **buffer = NULL;

	for (;;) {
		float nsamples = stb_vorbis_get_frame_float(f, NULL, &buffer);

		if (nsamples <= 0) break;

		while (capacity < nframes + nsamples) {
			capacity *= 2;
			AudioFrame *new_data = (AudioFrame *)memrealloc(data, capacity * sizeof(AudioFrame));
			if (new_data == NULL) {
				stb_vorbis_close(f);
				memfree(file_buf);
				memfree(data);
				data = NULL;
				valid = false;
				return -2;
			}
			data = new_data;
		}

		for (uint32_t i = 0; i < nsamples; ++i) {
			if (channels >= 2) {
				data[nframes + i] = AudioFrame(buffer[0][i], buffer[1][i]);
			} else {
				data[nframes + i] = AudioFrame(buffer[0][i], buffer[0][i]);
			}
		}

		nframes += nsamples;
	}

	memfree(file_buf);

	if (sample_rate != mix_rate) {
		stb_vorbis_close(f);
		return _resample_from(sample_rate);
	}

	if (capacity > nframes) {
		AudioFrame *fit_data = (AudioFrame *)memrealloc(data, nframes * sizeof(AudioFrame));
		if (fit_data == NULL) {
			memfree(data);
			data = NULL;
			valid = false;
			return -2;
		}
		data = fit_data;
		capacity = nframes;
	}

	stb_vorbis_close(f);
	return nframes;
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
		valid = false;
		return -2;
	}

	uint64_t mix_offset = 0;

	uint64_t mix_increment = uint64_t((source_rate / double(mix_rate)) * double(FP_LEN));

	for (int i = 0; i < new_length; ++i) {
		uint32_t idx = 4 + uint32_t(mix_offset >> FP_BITS);
		float mu = (mix_offset & FP_MASK) / float(FP_LEN);
		AudioFrame y0 = data[idx - 3];
		AudioFrame y1 = data[idx - 2];
		AudioFrame y2 = data[idx - 1];
		AudioFrame y3 = data[idx - 0];

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
	if (path.ends_with(".ogg")) {
		_decode_vorbis(path);
	}

	update_length();
}

bool AudioStreamRAM::is_valid() {
	return valid;
}

// uint32_t AudioStreamRAM::get_frame_count() {
//     return nframes;
// }

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
	// ClassDB::bind_method("get_frame_count", &AudioStreamRAM::get_frame_count);
}

AudioStreamPlaybackRAM::AudioStreamPlaybackRAM() :
		active(false), position(0) {
}

AudioStreamPlaybackRAM::~AudioStreamPlaybackRAM() {
}

void AudioStreamPlaybackRAM::stop() {
	active = false;
}

void AudioStreamPlaybackRAM::start(float p_from_pos) {
	ERR_FAIL_COND(!(base->valid));
	if (!(base->valid)) return;

	seek(p_from_pos);
	active = true;
}

void AudioStreamPlaybackRAM::seek(float p_time) {
	ERR_FAIL_COND(!(base->valid));
	if (!(base->valid)) return;

	uint32_t max = base->nframes;
	position = p_time * base->mix_rate;
	if (position < 0 || position >= max) {
		position = 0;
	}
}

void AudioStreamPlaybackRAM::mix(AudioFrame *p_buffer, float p_rate, int p_frames) {
	ERR_FAIL_COND(!active);
	if (!active) {
		return;
	}

	uint32_t max = base->nframes;
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
#include "audio_stream_ram.h"
#include "core/os/file_access.h"

int AudioStreamRAM::_decode_vorbis(String filename) {
	Error r_error;
	int error;

	Vector<uint8_t> file_buf = FileAccess::get_file_as_array(filename, &r_error);
	stb_vorbis *f = stb_vorbis_open_memory(file_buf.ptr(), file_buf.size(), &error, NULL);

	stb_vorbis_info info = stb_vorbis_get_info(f);

	if (f == NULL) return -1;

	uint32_t sample_rate = info.sample_rate;
	int channels = info.channels;

	float **buffer = NULL;

	data = (AudioFrame *)memalloc(SAFE_FRAMES * sizeof(AudioFrame));

	if (data == NULL) return -2;

	capacity = SAFE_FRAMES;
	nframes = 0;

	for (;;) {
		float nsamples = stb_vorbis_get_frame_float(f, NULL, &buffer);

		if (nsamples <= 0) break;

		while (capacity < nframes + nsamples) {
			capacity *= 2;
			AudioFrame *new_data = (AudioFrame *)memrealloc(data, capacity * sizeof(AudioFrame));
			if (new_data == NULL) {
				stb_vorbis_close(f);
				memfree(data);
				data = NULL;
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

	if (sample_rate != mix_rate) {
		stb_vorbis_close(f);
		return _resample_from(sample_rate);
	}

	if (capacity > nframes) {
		AudioFrame *fit_data = (AudioFrame *)memrealloc(data, nframes * sizeof(AudioFrame));
		if (fit_data == NULL) {
			memfree(data);
			data = NULL;
			return -2;
		}
		data = fit_data;
		capacity = nframes;
	}

	stb_vorbis_close(f);
	return nframes;
}
#include "audio_stream_ram.h"
#include "core/os/file_access.h"

#define WAVE_UINT32_RIFF 1179011410
#define WAVE_UINT32_WAVE 1163280727
#define WAVE_UINT32_FMT 544501094
#define WAVE_UINT32_FACT 1952670054
#define WAVE_UINT32_DATA 1635017060

#define WAVE_FORMAT_PCM 0x1
#define WAVE_FORMAT_IEEE 0x3
#define WAVE_FORMAT_IMA_ADPCM 0x11

typedef struct _RiffHeader {
	uint32_t riff_id;
	uint32_t file_size;
	uint32_t wave_id;
	uint8_t data;
} RiffHeader;

typedef struct _ChunkHeader {
	uint32_t chunk_id;
	uint32_t chunk_size;
	uint8_t data;
} ChunkHeader;

typedef struct _FmtChunk {
	uint16_t format;
	uint16_t channel_count;
	uint32_t sample_rate;
	uint32_t data_rate;
	uint16_t frame_size;
	uint16_t bit_depth;
} FmtChunk;

typedef struct _FactChunk {
	uint32_t sample_length;
} FactChunk;

static const int ima_index_table[16] = {
	-1, -1, -1, -1, // +0 / +3 : - the step
	2, 4, 6, 8, // +4 / +7 : + the step
	-1, -1, -1, -1, // -0 / -3 : - the step
	2, 4, 6, 8, // -4 / -7 : + the step
};

static const int ima_step_table[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34,
	37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
	544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
	1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
	4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
	27086, 29794, 32767
};

static inline float decode_nibble(uint8_t nibble, int16_t &p, int &s) {
	int diff = ima_step_table[s] >> 3;
	if (nibble & 4) diff += ima_step_table[s];
	if (nibble & 2) diff += ima_step_table[s] >> 1;
	if (nibble & 1) diff += ima_step_table[s] >> 2;
	if (nibble & 8) diff = -diff;
	p += diff;

	s += ima_index_table[nibble];
	if (s < 0)
		s = 0;
	else if (s > 88)
		s = 88;

	if (p < -32768)
		return -1;
	else if (p > 32767)
		return 1;
	return p / 32767.0f;
}

int AudioStreamRAM::_decode_wave(String filename) {
	Error r_error;

	Vector<uint8_t> file_buf = FileAccess::get_file_as_array(filename, &r_error);
	if (file_buf.size() < 12) return -1;

	const RiffHeader *header = (RiffHeader *)file_buf.ptr();
	if (header->riff_id != WAVE_UINT32_RIFF || header->wave_id != WAVE_UINT32_WAVE) return -1;

	if ((uint32_t)file_buf.size() - header->file_size != sizeof(uint32_t) * 2) {
		WARN_PRINT("mismatch reported file size");
	}

	uint8_t *ptr = (uint8_t *)&header->data;
	uint32_t index = 0;

	FmtChunk *fmt = NULL;
	FactChunk *fact = NULL;
	uint32_t data_size = 0;
	uint8_t *buffer = NULL;

	while (true) {
		ChunkHeader *chunk = (ChunkHeader *)(ptr + index);
		switch (chunk->chunk_id) {
			case WAVE_UINT32_FMT:
				if (
						chunk->chunk_size != 16 &&
						chunk->chunk_size != 18 &&
						chunk->chunk_size != 20 &&
						chunk->chunk_size != 40) {
					return -1;
				}

				fmt = (FmtChunk *)&chunk->data;
				break;
			case WAVE_UINT32_FACT:
				fact = (FactChunk *)&chunk->data;
				break;
			case WAVE_UINT32_DATA:
				data_size = chunk->chunk_size;
				buffer = (uint8_t *)&chunk->data;
				break;
		}
		index += chunk->chunk_size + sizeof(uint32_t) * 2;
		if (index + 12 + sizeof(uint32_t) * 2 >= file_buf.size()) {
			break;
		}
	}

	if (!fmt) return -1;

	if (fmt->format == WAVE_FORMAT_IMA_ADPCM) {
		if (!fact) return -1;
		// ADPCM
		uint32_t frame_offset = 0;
		uint32_t frame_count = data_size / fmt->frame_size;

		capacity = nframes = fact->sample_length * 2;
		data = (AudioFrame *)memalloc(capacity * sizeof(AudioFrame));
		if (data == NULL) return -2;

		for (uint32_t i = 0; i < frame_count; ++i) {
			uint16_t nchannels = fmt->channel_count > 2 ? 2 : fmt->channel_count;
			bool is_single_channel = nchannels == 1;

			AudioFrame *data_buf = data + frame_offset;
			for (uint16_t ch = 0; ch < nchannels; ch++) {
				const int byte_offset = ch * 4;

				int16_t predictor = ((int16_t)buffer[byte_offset + 1] << 8) | buffer[byte_offset];
				int step_index = buffer[byte_offset + 2];

				uint8_t reserved = buffer[byte_offset + 3];
				if (reserved != 0) return -1;

				int byte_index = fmt->channel_count * 4 + byte_offset;
				int idx = 0;

				while (byte_index < fmt->frame_size) {
					for (int j = 0; j < 4; j++) {
						data_buf[idx][ch] = decode_nibble(buffer[byte_index] & 0xf, predictor, step_index);
						if (is_single_channel) data_buf[idx][ch + 1] = data_buf[idx][ch];
						++idx;
						data_buf[idx][ch] = decode_nibble(buffer[byte_index] >> 4, predictor, step_index);
						if (is_single_channel) data_buf[idx][ch + 1] = data_buf[idx][ch];
						++idx;
						byte_index++;
					}
					byte_index += (fmt->channel_count - 1) << 2;
				}
			}

			buffer += fmt->frame_size;
			frame_offset += fmt->frame_size - 8;
		}
	} else if (fmt->format == WAVE_FORMAT_PCM || fmt->format == WAVE_FORMAT_IEEE) {
		capacity = nframes = data_size / fmt->frame_size;
		data = (AudioFrame *)memalloc(capacity * sizeof(AudioFrame));
		if (data == NULL) return -2;

		bool is_single_channel = fmt->channel_count == 1;

		for (int i = 0; i < nframes; ++i) {
			for (int ch = 0; ch < 2; ch++) {
				uint8_t *value = (uint8_t *)buffer + i * fmt->frame_size;
				if (!is_single_channel) {
					value += ch * (fmt->bit_depth / 8);
				}

				switch (fmt->bit_depth) {
					case 8:
						data[i][ch] = (*value - 128) * (1.0f / 127.0f);
						break;
					case 16:
						data[i][ch] = (*(int16_t *)value) / 32767.0f;
						break;
					case 24: {
						int32_t x = (value[0] << 0) | (value[1] << 8) | (value[2] << 16);
						int32_t v = (x) | (!!((x)&0x800000) * 0xff000000);
						data[i][ch] = (v) / 8388608.0f;
						break;
					}
					case 32:
						if (fmt->format == WAVE_FORMAT_IEEE)
							data[i][ch] = (*(float *)value);
						else
							data[i][ch] = (*(int32_t *)value) / 2147483648.0f;
						break;
					case 64:
						if (fmt->format == WAVE_FORMAT_IEEE)
							data[i][ch] = (*(double *)value);
						else
							data[i][ch] = (*(int64_t *)value) / 9223372036854775807.0;
				}
			}
		}
	} else {
		return -1;
	}

	if (fmt->sample_rate != mix_rate) {
		return _resample_from(fmt->sample_rate);
	}
	return 0;
}
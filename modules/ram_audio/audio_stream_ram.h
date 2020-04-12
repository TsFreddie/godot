#ifndef AUDIO_STREAM_RAM_H
#define AUDIO_STREAM_RAM_H

#include "core/reference.h"
#include "core/resource.h"
#include "servers/audio/audio_stream.h"
#include "core/math/audio_frame.h"

class AudioStreamRAM: public AudioStream {
    GDCLASS(AudioStreamRAM, AudioStream)

	enum {
		FP_BITS = 16, //fixed point used for resampling
		FP_LEN = (1 << FP_BITS),
		FP_MASK = FP_LEN - 1,
        SAFE_FRAMES = 8192 // empty frames reserved
	};

private:
    friend class AudioStreamPlaybackRAM;
    bool valid;
    AudioFrame *data;
    uint32_t nframes;
    uint32_t capacity;
    float length;
    int mix_rate;
    // void _premix(int16_t *pcm_data, float position);
    int _decode_vorbis(String filename);
    int _resample_from(int source_rate);
    void update_length();

public:
    AudioStreamRAM();
    ~AudioStreamRAM();

    void load(String path);
    bool is_valid();
    // uint32_t get_frame_count();

    virtual Ref<AudioStreamPlayback> instance_playback();
    virtual String get_stream_name() const;
    virtual float get_length() const;

protected:
	static void _bind_methods();
};

class AudioStreamPlaybackRAM : public AudioStreamPlayback {
    GDCLASS(AudioStreamPlaybackRAM, AudioStreamPlayback)
    friend class AudioStreamRAM;

private:
    Ref<AudioStreamRAM> base;
    bool active;
    uint32_t position;

public:
    virtual void start(float p_from_pos = 0.0);
    virtual void stop();
    virtual bool is_playing() const;
    virtual int get_loop_count() const;
    virtual float get_playback_position() const;
    virtual void seek(float p_time);
    virtual void mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames);
    virtual float get_length() const;
    AudioStreamPlaybackRAM();
    ~AudioStreamPlaybackRAM();
};

#endif

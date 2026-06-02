#include "AudioPlayer.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdint>

AudioPlayer::AudioPlayer()
{
    initialize();
}

AudioPlayer::AudioPlayer(const std::string& dump_prefix)
    : dump_prefix_(dump_prefix)
{
    initialize();
    if (!dump_prefix_.empty()) {
        std::cout << "[AudioPlayer] dump-to-wav enabled, prefix=" << dump_prefix_ << std::endl;
    }
}

AudioPlayer::~AudioPlayer()
{
    cleanup();
}

bool AudioPlayer::initialize()
{
    if (initialized_)
        return true;

    int err = snd_pcm_open(&pcm_handle_, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        std::cerr << "ALSA Error: Cannot open PCM device: " << snd_strerror(err) << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

void AudioPlayer::play(const int16_t *audioData, int audio_len, float speed)
{
    // audio_len here is in BYTES (caller passes msg.length * sizeof(int16_t));
    // each int16 sample = 2 bytes, so #samples = audio_len / 2.
    const int samples = audio_len / 2;

    // Dump-to-wav path: independent of ALSA, works even on headless / no-soundcard hosts.
    if (!dump_prefix_.empty() && samples > 0 && audioData != nullptr) {
        write_wav(audioData, samples);
    }

    if (!initialized_ || !pcm_handle_)
        return;

    unsigned int sample_rate = static_cast<unsigned int>(16000 * speed);
    int err = snd_pcm_set_params(pcm_handle_,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 1,
                                 sample_rate,
                                 1,
                                 50000);
    if (err < 0)
    {
        std::cerr << "ALSA Error: Cannot set parameters: " << snd_strerror(err) << std::endl;
        return;
    }

    const snd_pcm_uframes_t frames = audio_len / 2;
    const int max_retries = 3;
    int retry_count = 0;

    while (true)
    {
        err = snd_pcm_writei(pcm_handle_, audioData, frames);
        if (err == -EPIPE)
        {
            if (++retry_count >= max_retries)
                break;
            snd_pcm_prepare(pcm_handle_);
        }
        else if (err < 0)
        {
            break;
        }
        else
        {
            break;
        }
    }

    if (snd_pcm_state(pcm_handle_) == SND_PCM_STATE_RUNNING)
    {
        snd_pcm_drain(pcm_handle_);
    }
}

void AudioPlayer::cleanup()
{
    if (pcm_handle_)
    {
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
    initialized_ = false;
}

// Write a minimal canonical PCM WAVE file: 16kHz, mono, S16_LE.
// One file per call. Caller owns/manages `data`.
void AudioPlayer::write_wav(const int16_t* data, int samples)
{
    char filename[1024];
    std::snprintf(filename, sizeof(filename), "%s_%04d.wav",
                  dump_prefix_.c_str(), ++dump_counter_);

    std::ofstream f(filename, std::ios::binary);
    if (!f) {
        std::cerr << "[AudioPlayer] failed to open " << filename << std::endl;
        return;
    }

    const uint32_t sample_rate = 16000;
    const uint16_t channels    = 1;
    const uint16_t bits        = 16;
    const uint32_t byte_rate   = sample_rate * channels * bits / 8;
    const uint16_t block_align = channels * bits / 8;
    const uint32_t data_bytes  = static_cast<uint32_t>(samples) * sizeof(int16_t);
    const uint32_t riff_size   = 36 + data_bytes;
    const uint32_t fmt_size    = 16;
    const uint16_t audio_format = 1;

    auto w = [&](const void* p, std::streamsize n) {
        f.write(reinterpret_cast<const char*>(p), n);
    };

    f.write("RIFF", 4); w(&riff_size, 4); f.write("WAVE", 4);
    f.write("fmt ", 4); w(&fmt_size, 4);
    w(&audio_format, 2); w(&channels, 2);
    w(&sample_rate, 4);  w(&byte_rate, 4);
    w(&block_align, 2);  w(&bits, 2);
    f.write("data", 4); w(&data_bytes, 4);
    w(data, data_bytes);

    std::cout << "[AudioPlayer] dumped " << filename
              << "  (" << samples << " samples, "
              << (samples / 16000.0) << " s)" << std::endl;
}
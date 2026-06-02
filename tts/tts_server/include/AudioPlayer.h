#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <cstdint>
#include <string>
#include <alsa/asoundlib.h>

class AudioPlayer {
public:
    AudioPlayer();
    explicit AudioPlayer(const std::string& dump_prefix);
    ~AudioPlayer();
    
    bool initialize();
    void play(const int16_t* audioData, int audio_len, float speed = 1.0f);
    
private:
    void cleanup();
    void write_wav(const int16_t* data, int samples);
    
    snd_pcm_t* pcm_handle_ = nullptr;
    bool initialized_ = false;

    std::string dump_prefix_;
    int dump_counter_ = 0;
};

#endif // AUDIO_PLAYER_H
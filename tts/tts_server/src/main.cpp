#include "TTSModel.h"
#include "MessageQueue.h"
#include "AudioPlayer.h"
#include "TextProcessor.h"
#include "Utils.h"
#include "ZmqServer.h"
#include "ZmqPuller.h"
#include "WavWriter.h"

#include <thread>
#include <iostream>
#include <atomic>
#include <memory>
#include <cstdlib>
#include <string>
#include <filesystem>

zmq_component::ZmqPuller text_pull("tcp://*:7777");
zmq_component::ZmqServer status_server("tcp://*:6677");
std::atomic<bool> first_msg(true);
std::atomic<int> wav_save_index{0};

static const char* tts_save_wav_dir() {
    const char* dir = std::getenv("TTS_SAVE_WAV_DIR");
    return (dir && *dir) ? dir : nullptr;
}

static bool tts_skip_playback() {
    const char* v = std::getenv("TTS_SKIP_PLAYBACK");
    return v && *v && std::string(v) != "0";
}

void synthesis_worker(DoubleMessageQueue &queue, TTSModel &model) {
    // utils::set_realtime_priority(pthread_self(), 99);

    while (true) {
        std::string text = queue.pop_text();
        if (text.empty()) break;

        if (text.find("END") != std::string::npos) {
            first_msg = true;
            size_t end_pos = text.find("END");
            text = text.substr(0, end_pos);
        }

        int32_t audio_len = 0;
        if (!text.empty()) {
            std::cout << "[TTS infer] Inferring text: " << text << std::endl;
            int16_t* wavData = model.infer(text, audio_len);
            
            if (wavData && audio_len > 0) {
                auto audio_data = std::make_unique<int16_t[]>(audio_len);
                memcpy(audio_data.get(), wavData, audio_len * sizeof(int16_t));

                if (const char* out_dir = tts_save_wav_dir()) {
                    std::error_code ec;
                    std::filesystem::create_directories(out_dir, ec);
                    const int idx = wav_save_index.fetch_add(1) + 1;
                    std::string path = std::string(out_dir) + "/tts_out_" +
                                       std::to_string(idx) + ".wav";
                    if (WritePcm16Wav(path, audio_data.get(), audio_len, 16000)) {
                        std::cout << "[TTS] 已保存 WAV: " << path << std::endl;
                    } else {
                        std::cerr << "[TTS] 保存 WAV 失败: " << path << std::endl;
                    }
                }

                queue.push_audio(std::move(audio_data), audio_len, first_msg);
                model.free_data(wavData);
            }
        } else {
            auto empty_audio = std::make_unique<int16_t[]>(0);
            queue.push_audio(std::move(empty_audio), 0, first_msg);
        }
    }
}

void playback_worker(DoubleMessageQueue &queue, AudioPlayer &player) {
    const bool skip_play = tts_skip_playback();
    if (skip_play) {
        std::cout << "[TTS] TTS_SKIP_PLAYBACK=1，不在服务器上播放，仅落盘/队列\n";
    }
    while (true) {
        auto msg = queue.pop_audio();
        if (msg.data == nullptr) break;

        if (!skip_play) {
            player.play(msg.data.get(), msg.length * sizeof(int16_t), 1.0f);
        }
        
        if (msg.is_last) {
            status_server.send("[tts -> voice]play end success");
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model_path>" << std::endl;
        return 1;
    }

    try {
    
        TTSModel model(argv[1]);
        AudioPlayer player;
        DoubleMessageQueue queue;

        std::thread synthesis_thread(synthesis_worker, std::ref(queue), std::ref(model));
        std::thread playback_thread(playback_worker, std::ref(queue), std::ref(player));

        while (true) {
            if (first_msg) {
                std::string req = status_server.receive();
                std::cout << "[voice -> tts] received: " << req << std::endl;
            }
            first_msg = false;

            std::string text = text_pull.pull();
            std::cout << "[llm -> tts] received: " << text << std::endl;

            if (!text.empty() && text.find("<think>") == std::string::npos) {
              
    
                queue.push_text(text);
            }
        }

        // 清理
        queue.stop();
        synthesis_thread.join();
        playback_thread.join();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
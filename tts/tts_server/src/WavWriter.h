#pragma once

#include <cstdint>
#include <fstream>
#include <string>

/** 将 int16 单声道 PCM 写成标准 WAV（16kHz 默认，与 TTS 输出一致） */
inline bool WritePcm16Wav(const std::string &path, const int16_t *data,
                          int32_t num_samples, int32_t sample_rate = 16000) {
  if (!data || num_samples <= 0) {
    return false;
  }

  struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint32_t sample_rate_hz = 16000;
    uint32_t byte_rate = 32000;
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    char data_tag[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
  } header;

  header.sample_rate_hz = static_cast<uint32_t>(sample_rate);
  header.byte_rate =
      header.sample_rate_hz * header.num_channels * header.bits_per_sample / 8;
  header.block_align =
      header.num_channels * header.bits_per_sample / 8;
  header.data_size =
      static_cast<uint32_t>(num_samples * sizeof(int16_t));
  header.chunk_size = 36 + header.data_size;

  std::ofstream os(path, std::ios::binary);
  if (!os) {
    return false;
  }
  os.write(reinterpret_cast<const char *>(&header), sizeof(header));
  os.write(reinterpret_cast<const char *>(data),
           num_samples * sizeof(int16_t));
  return static_cast<bool>(os);
}

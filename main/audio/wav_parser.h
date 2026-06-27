// WAV (RIFF/WAVE) 文件头解析器
//   - 跳过 RIFF 头和 fmt chunk（可能含 "LIST"/"JUNK"/"bext" 等可选 chunk）
//   - 找到 "data" chunk 起始位置 + sample_rate/channels/bits_per_sample
//   - 不支持扩展格式（IEEE float、ADPCM、μ-law 等）—— 仅标准 PCM
//
// 用法：
//   WavHeaderInfo info;
//   if (WavParseHeader(fp, info)) {
//       fseek(fp, info.data_offset, SEEK_SET);
//       // 用 ESP_PCM_DEC 喂 PCM data
//   }
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>

struct WavHeaderInfo {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_offset = 0;     // PCM data 在文件中的起始偏移
    uint32_t data_size = 0;       // PCM data 字节数
    bool valid = false;           // 解析是否成功
};

inline uint32_t ReadLE32(FILE* fp) {
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

inline uint16_t ReadLE16(FILE* fp) {
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

inline bool WavParseHeader(FILE* fp, WavHeaderInfo& out) {
    out = {};  // reset

    // 1. RIFF header (12 bytes)
    char riff[5] = {0};
    char wave[5] = {0};
    if (fread(riff, 1, 4, fp) != 4) return false;
    if (memcmp(riff, "RIFF", 4) != 0) return false;
    uint32_t riff_size = ReadLE32(fp);  // unused
    (void)riff_size;
    if (fread(wave, 1, 4, fp) != 4) return false;
    if (memcmp(wave, "WAVE", 4) != 0) return false;

    // 2. 遍历 chunks
    bool fmt_found = false;
    while (true) {
        // 读 chunk id (4 bytes)
        char id[5] = {0};
        if (fread(id, 1, 4, fp) != 4) return false;  // EOF
        uint32_t chunk_size = ReadLE32(fp);
        uint32_t chunk_start = (uint32_t)ftell(fp);

        if (memcmp(id, "fmt ", 4) == 0) {
            // fmt chunk
            uint16_t audio_format = ReadLE16(fp);
            if (audio_format != 1) {
                // 不支持非 PCM（ADPCM=2/17, IEEE float=3, ...）
                return false;
            }
            out.channels = ReadLE16(fp);
            out.sample_rate = ReadLE32(fp);
            ReadLE32(fp);  // byte_rate
            ReadLE16(fp);  // block_align
            out.bits_per_sample = ReadLE16(fp);
            fmt_found = true;
        } else if (memcmp(id, "data", 4) == 0) {
            // data chunk - 找到 data 起点
            out.data_offset = chunk_start;
            out.data_size = chunk_size;
            out.valid = fmt_found;
            return out.valid;
        }
        // 其它 chunk (LIST/JUNK/bext/fact...)：跳过
        //   chunk_size 可能为奇数（RIFF 规则），所以对齐到 2 字节
        uint32_t skip = chunk_size + (chunk_size & 1);
        if (fseek(fp, skip, SEEK_CUR) != 0) return false;
    }
}

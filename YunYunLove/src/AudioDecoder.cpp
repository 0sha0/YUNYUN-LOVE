#include "AudioDecoder.h"
#include "PathUtil.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

// stb_vorbis 以独立编译单元（..\deps\stb_vorbis.c）加入工程；
// 这里只声明用到的入口，避免把 20 万行实现拖进本 TU。
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len,
                                        int* channels, int* sample_rate, short** output);

namespace AudioDecoder
{
    // ---------- 读取整个文件 ----------
    static bool ReadAll(const std::wstring& path, std::vector<uint8_t>* out, std::string* error)
    {
        FILE* f = _wfopen(PathUtil::Long(path).c_str(), L"rb");
        if (!f)
        {
            *error = "open failed";
            return false;
        }
        _fseeki64(f, 0, SEEK_END);
        long long sz = _ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 512ll * 1024 * 1024)
        {
            fclose(f);
            *error = "bad size";
            return false;
        }
        out->resize((size_t)sz);
        size_t rd = fread(out->data(), 1, (size_t)sz, f);
        fclose(f);
        if (rd != (size_t)sz)
        {
            *error = "read failed";
            return false;
        }
        return true;
    }

    // ---------- 格式探测（文件头优先，扩展名兜底） ----------
    enum class Fmt { Unknown, Ogg, Wav, Mp3 };

    static Fmt Sniff(const std::vector<uint8_t>& b, const std::wstring& path)
    {
        auto has = [&](size_t off, const char* sig, size_t n) {
            return b.size() >= off + n && memcmp(b.data() + off, sig, n) == 0;
        };
        if (has(0, "OggS", 4))
            return Fmt::Ogg;
        if (has(0, "RIFF", 4) && has(8, "WAVE", 4))
            return Fmt::Wav;
        if (has(0, "ID3", 3) || (b.size() > 1 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0))
            return Fmt::Mp3;
        std::wstring low = path;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        if (low.size() > 4)
        {
            std::wstring ext = low.substr(low.size() - 4);
            if (ext == L".ogg") return Fmt::Ogg;
            if (ext == L".wav") return Fmt::Wav;
            if (ext == L".mp3") return Fmt::Mp3;
        }
        return Fmt::Unknown;
    }

    static void DecodeMp3Bytes(const std::vector<uint8_t>& b, PcmData* r)
    {
        drmp3_config cfg = {};
        drmp3_uint64 frames = 0;
        float* data = drmp3_open_memory_and_read_pcm_frames_f32(
            (const void*)b.data(), b.size(), &cfg, &frames, nullptr);
        if (!data || !frames || !cfg.channels || !cfg.sampleRate)
        {
            if (data)
                drmp3_free(data, nullptr);
            r->error = "mp3 decode failed";
            return;
        }
        r->pcm.assign(data, data + (size_t)frames * cfg.channels);
        drmp3_free(data, nullptr);
        r->channels = (int)cfg.channels;
        r->sampleRate = (int)cfg.sampleRate;
        r->ok = true;
    }

    static void DecodeOggBytes(const std::vector<uint8_t>& b, PcmData* r)
    {
        if (b.size() > (size_t)INT32_MAX)
        {
            r->error = "ogg too large";
            return;
        }
        int ch = 0, rate = 0;
        short* out = nullptr;
        int frames = stb_vorbis_decode_memory(b.data(), (int)b.size(), &ch, &rate, &out);
        if (frames <= 0 || !out || ch <= 0 || rate <= 0)
        {
            if (out)
                free(out);
            r->error = "ogg decode failed";
            return;
        }
        const size_t total = (size_t)frames * (size_t)ch;
        r->pcm.resize(total);
        const float inv = 1.0f / 32768.0f;
        for (size_t i = 0; i < total; i++)
            r->pcm[i] = (float)out[i] * inv;
        free(out);
        r->channels = ch;
        r->sampleRate = rate;
        r->ok = true;
    }

    // 简易 WAV 解析：PCM 8/16/24/32-bit + IEEE float32
    static void DecodeWavBytes(const std::vector<uint8_t>& b, PcmData* r)
    {
        auto u16 = [&](size_t o) -> uint16_t { return (uint16_t)(b[o] | (b[o + 1] << 8)); };
        auto u32 = [&](size_t o) -> uint32_t {
            return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) |
                   ((uint32_t)b[o + 3] << 24);
        };
        if (b.size() < 44)
        {
            r->error = "wav too small";
            return;
        }
        size_t pos = 12;
        uint16_t fmtTag = 0, ch = 0, bits = 0;
        uint32_t rate = 0;
        size_t dataOff = 0, dataLen = 0;
        while (pos + 8 <= b.size())
        {
            char id[4];
            memcpy(id, b.data() + pos, 4);
            uint32_t sz = u32(pos + 4);
            size_t body = pos + 8;
            if (memcmp(id, "fmt ", 4) == 0 && body + 16 <= b.size())
            {
                fmtTag = u16(body + 0);
                ch = u16(body + 2);
                rate = u32(body + 4);
                bits = u16(body + 14);
                if (fmtTag == 0xFFFE && body + 26 <= b.size())
                    fmtTag = u16(body + 24);   // WAVE_FORMAT_EXTENSIBLE → 子格式
            }
            else if (memcmp(id, "data", 4) == 0)
            {
                dataOff = body;
                dataLen = sz;
                if (dataOff + dataLen > b.size())
                    dataLen = b.size() - dataOff;
            }
            pos = body + sz + (sz & 1);
        }
        if (!dataOff || !dataLen || !ch || !rate || !bits)
        {
            r->error = "wav header invalid";
            return;
        }
        const int bytesPer = bits / 8;
        if (bytesPer <= 0)
        {
            r->error = "wav bits unsupported";
            return;
        }
        const size_t frames = dataLen / (size_t)(bytesPer * ch);
        if (frames == 0)
        {
            r->error = "wav empty";
            return;
        }
        r->pcm.resize(frames * ch);
        const uint8_t* p = b.data() + dataOff;
        for (size_t i = 0; i < frames * ch; i++)
        {
            const uint8_t* s = p + i * bytesPer;
            float v = 0.f;
            if (fmtTag == 3 && bits == 32)
            {
                float fv;
                memcpy(&fv, s, 4);
                v = fv;
            }
            else if (bits == 8)
            {
                v = ((float)s[0] - 128.0f) / 128.0f;
            }
            else if (bits == 16)
            {
                int16_t iv;
                memcpy(&iv, s, 2);
                v = (float)iv / 32768.0f;
            }
            else if (bits == 24)
            {
                int32_t iv = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24);
                v = (float)(iv >> 8) / 8388608.0f;
            }
            else if (bits == 32)
            {
                int32_t iv;
                memcpy(&iv, s, 4);
                v = (float)iv / 2147483648.0f;
            }
            else
            {
                r->error = "wav bits unsupported";
                return;
            }
            r->pcm[i] = v;
        }
        r->channels = ch;
        r->sampleRate = (int)rate;
        r->ok = true;
    }

    PcmData DecodeFile(const std::wstring& path)
    {
        PcmData r;
        std::vector<uint8_t> bytes;
        std::string err;
        if (!ReadAll(path, &bytes, &err))
        {
            r.error = err;
            return r;
        }
        switch (Sniff(bytes, path))
        {
        case Fmt::Ogg: DecodeOggBytes(bytes, &r); break;
        case Fmt::Wav: DecodeWavBytes(bytes, &r); break;
        case Fmt::Mp3: DecodeMp3Bytes(bytes, &r); break;
        default:       r.error = "unsupported audio format"; break;
        }
        if (r.ok && (r.channels <= 0 || r.sampleRate <= 0 || r.FrameCount() <= 0))
        {
            r.ok = false;
            r.error = "decoded empty";
        }
        return r;
    }

    PcmData DecodeMp3File(const std::wstring& path)
    {
        return DecodeFile(path);
    }
}

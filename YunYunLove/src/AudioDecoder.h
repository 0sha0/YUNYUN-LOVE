#pragma once
// ============================================================
// AudioDecoder.h — 本地音频解码（ogg / wav / mp3 → 交错 float PCM）
//   不再依赖 UnityWebRequestMultimedia（实测 file:// ogg 解出空 clip：
//   loadState=Loaded 但 samples=0）。自解码后用 AudioClip.Create + SetData
//   构造，行为确定、可预期。
// ============================================================
#include <string>
#include <vector>

namespace AudioDecoder
{
    struct PcmData
    {
        std::vector<float> pcm;      // 交错采样（-1..1）
        int channels = 0;
        int sampleRate = 0;
        std::string error;
        bool ok = false;

        int FrameCount() const
        {
            return channels > 0 ? (int)(pcm.size() / (size_t)channels) : 0;
        }
    };

    // 兼容旧名（仅 mp3 时代）
    using Mp3Data = PcmData;

    // 按扩展名 + 文件头自动识别并解码
    PcmData DecodeFile(const std::wstring& path);

    // 仅 mp3（保留旧接口）
    PcmData DecodeMp3File(const std::wstring& path);
}

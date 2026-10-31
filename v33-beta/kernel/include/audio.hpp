#pragma once
#include <stdint.h>
#include <stddef.h>

enum class AudioBackend : uint8_t {
    None = 0,
    AC97 = 1,
    HdaDetectedUnsupported = 2,
};

enum class SystemSound : uint8_t {
    Startup = 0,
    Notify = 1,
    Error = 2,
    Click = 3,
};

struct AudioStats {
    bool controllerFound;
    bool initialized;
    bool playing;
    bool muted;
    AudioBackend backend;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t mixerBase;
    uint16_t busMasterBase;
    uint32_t sampleRate;
    uint64_t tonesPlayed;
    uint64_t pcmSubmissions;
    uint64_t pcmFramesSubmitted;
    uint64_t pcmFramesPlayed;
    uint64_t underruns;
    uint64_t systemSounds;
    uint8_t volumePercent;
    uint32_t queuedFrames;
};

bool audio_init();
bool audio_play_tone(uint32_t frequencyHz, uint32_t durationMs);
bool audio_submit_pcm_s16_stereo(const int16_t* samples, uint32_t frames, uint32_t sampleRate);
bool audio_play_system_sound(SystemSound sound);
bool audio_set_volume(uint8_t percent);
bool audio_set_mute(bool mute);
void audio_stop();
void audio_poll();
AudioStats audio_stats();

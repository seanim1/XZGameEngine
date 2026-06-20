#pragma once
#include <SDL3/SDL_audio.h>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace XZAudio {

static const float PI = 3.14159265f;

static std::vector<float> generate_parry_clash(float sample_rate = 44100.0f) {
    float duration = 0.6f;
    int   samples  = (int)(sample_rate * duration);
    std::vector<float> buf(samples);

    for (int i = 0; i < samples; i++) {
        float t = i / sample_rate;

        // --- Impact transient ---
        float impact_env = expf(-t * 80.0f);
        float impact     = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * impact_env;

        // --- Metal ring — shifted higher, more high partials ---
        float ring_env = expf(-t * 4.0f);  // slower decay so highs have time to sing

        float r1 = sinf(2*PI * 800.0f  * t) * 0.25f;  // higher fundamental
        float r2 = sinf(2*PI * 1600.0f * t) * 0.20f;  // inharmonic partial
        float r3 = sinf(2*PI * 2400.0f * t) * 0.18f;  // inharmonic partial
        float r4 = sinf(2*PI * 3800.0f * t) * 0.15f;  // high shimmer
        float r5 = sinf(2*PI * 5200.0f * t) * 0.12f;  // higher shimmer
        float r6 = sinf(2*PI * 7000.0f * t) * expf(-t * 15.0f) * 0.10f;  // bright attack
        float r7 = sinf(2*PI * 9500.0f * t) * expf(-t * 25.0f) * 0.08f;  // very bright
        float r8 = sinf(2*PI * 12000.0f* t) * expf(-t * 40.0f) * 0.05f;  // air/presence

        float ring = (r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8) * ring_env;

        // --- Scrape / friction ---
        float scrape_env = expf(-t * 20.0f) * (1.0f - expf(-t * 40.0f));
        float scrape     = ((float)rand() / RAND_MAX * 2.0f - 1.0f)
                         * scrape_env * 0.15f;

        // Mix
        float out = impact * 0.4f + ring + scrape;

        // Soft clip
        out = tanhf(out * 1.2f) * 0.85f;

        buf[i] = out;
    }
    return buf;
}

struct Sound {
    SDL_AudioStream*   stream = nullptr;
    std::vector<float> pcm;

    bool init(float sample_rate = 44100.0f) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("XZAudio: SDL_InitSubSystem audio failed: %s", SDL_GetError());
            return false;
        }
        SDL_AudioSpec spec{
            .format   = SDL_AUDIO_F32,
            .channels = 1,
            .freq     = (int)sample_rate
        };
        stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream) {
            SDL_Log("XZAudio: failed to open stream: %s", SDL_GetError());
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(stream)) {
            SDL_Log("XZAudio: failed to resume: %s", SDL_GetError());
            return false;
        }
        return true;
    }

    void setPCM(std::vector<float> data) { pcm = std::move(data); }

    void play() {
        if (!stream || pcm.empty()) return;
        SDL_PutAudioStreamData(stream, pcm.data(),
            (int)(pcm.size() * sizeof(float)));
    }

    ~Sound() {
        if (stream) SDL_DestroyAudioStream(stream);
    }
};

} // namespace XZAudio
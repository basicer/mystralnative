/**
 * Web Audio API Implementation
 *
 * Provides AudioContext, AudioBufferSourceNode, GainNode using SDL3 audio.
 * Implements a subset of the W3C Web Audio API specification.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

struct SDL_AudioStream;

namespace mystral {
namespace audio {

// Forward declarations
class AudioContext;
class AudioNode;
class AudioBuffer;
class AudioBufferSourceNode;
class GainNode;
class AudioDestinationNode;

/**
 * AudioBuffer - holds decoded audio data
 */
class AudioBuffer {
public:
    AudioBuffer(float sampleRate, int numberOfChannels, size_t length);
    ~AudioBuffer();

    float sampleRate() const { return sampleRate_; }
    int numberOfChannels() const { return numberOfChannels_; }
    size_t length() const { return length_; }
    double duration() const { return static_cast<double>(length_) / sampleRate_; }

    // Get channel data (returns pointer to float samples)
    float* getChannelData(int channel);
    const float* getChannelData(int channel) const;

    // Set data from interleaved samples
    void setFromInterleaved(const float* data, size_t numSamples, int numChannels);

private:
    float sampleRate_;
    int numberOfChannels_;
    size_t length_;  // Number of sample frames
    std::vector<std::vector<float>> channelData_;
};

/**
 * AudioParam - represents an audio parameter that can be automated
 */
class AudioParam {
public:
    AudioParam(float defaultValue = 1.0f);

    float value() const { return value_; }
    void setValue(float v) { value_ = v; }

    // For future: automation methods
    // void setValueAtTime(float value, double time);
    // void linearRampToValueAtTime(float value, double time);

private:
    float value_;
    float defaultValue_;
};

/**
 * AudioNode - base class for all audio nodes
 */
class AudioNode {
public:
    AudioNode(AudioContext* context);
    virtual ~AudioNode() = default;

    AudioContext* context() const { return context_; }

    virtual void connect(AudioNode* destination);
    virtual void disconnect();

    // For audio processing
    virtual void process(float* output, size_t numFrames, int numChannels) {}

protected:
    AudioContext* context_;
    std::vector<AudioNode*> outputs_;
};

/**
 * AudioDestinationNode - represents the final audio output
 */
class AudioDestinationNode : public AudioNode {
public:
    AudioDestinationNode(AudioContext* context);

    int maxChannelCount() const { return 2; }  // Stereo output
};

/**
 * GainNode - adjusts audio volume
 */
class GainNode : public AudioNode {
public:
    GainNode(AudioContext* context);

    AudioParam& gain() { return gain_; }
    const AudioParam& gain() const { return gain_; }

    void process(float* output, size_t numFrames, int numChannels) override;

private:
    AudioParam gain_;
};

/**
 * AudioBufferSourceNode - plays an AudioBuffer
 */
class AudioBufferSourceNode : public AudioNode {
public:
    AudioBufferSourceNode(AudioContext* context);
    ~AudioBufferSourceNode();

    void setBuffer(std::shared_ptr<AudioBuffer> buffer);
    std::shared_ptr<AudioBuffer> buffer() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_;
    }

    bool loop() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loop_;
    }
    void setLoop(bool loop) {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = loop;
    }

    double loopStart() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loopStart_;
    }
    void setLoopStart(double time) {
        std::lock_guard<std::mutex> lock(mutex_);
        loopStart_ = time;
    }

    double loopEnd() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loopEnd_;
    }
    void setLoopEnd(double time) {
        std::lock_guard<std::mutex> lock(mutex_);
        loopEnd_ = time;
    }

    // Playback control
    void start(double when = 0, double offset = 0, double duration = -1);
    void stop(double when = 0);

    bool isPlaying() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return isPlaying_;
    }

    // Event callback
    std::function<void()> onended;

    void process(float* output, size_t numFrames, int numChannels) override;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<AudioBuffer> buffer_;
    bool loop_ = false;
    double loopStart_ = 0;
    double loopEnd_ = 0;
    bool isPlaying_ = false;
    size_t playbackPosition_ = 0;
    double startTime_ = 0;
    double stopTime_ = -1;
    double offsetTime_ = 0;
    double durationTime_ = -1;
};

/**
 * AudioContext - main interface for Web Audio API
 */
class AudioContext {
public:
    AudioContext();
    ~AudioContext();

    // State
    enum class State { Suspended, Running, Closed };
    State state() const { return state_; }

    // Properties
    float sampleRate() const { return sampleRate_; }
    double currentTime() const;
    AudioDestinationNode* destination() { return destination_.get(); }

    // Factory methods
    std::shared_ptr<AudioBuffer> createBuffer(int numberOfChannels, size_t length, float sampleRate);
    std::unique_ptr<AudioBufferSourceNode> createBufferSource();
    std::unique_ptr<GainNode> createGain();

    // Decode audio data (async in browser, sync here for simplicity)
    std::shared_ptr<AudioBuffer> decodeAudioDataSync(const uint8_t* data, size_t length);

    // Lifecycle
    void resume();
    void suspend();
    void close();

    // Internal: register/unregister active source nodes
    void registerSource(AudioBufferSourceNode* source);
    void unregisterSource(AudioBufferSourceNode* source);

private:
    void audioCallback(float* output, int numFrames);
    static void sdlAudioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);

    State state_ = State::Suspended;
    float sampleRate_ = 44100.0f;
    uint64_t startTime_ = 0;
    std::atomic<uint64_t> sampleCount_{0};

    std::unique_ptr<AudioDestinationNode> destination_;
    std::vector<AudioBufferSourceNode*> activeSources_;
    std::mutex sourcesMutex_;

    // SDL audio
    uint32_t audioDevice_ = 0;
    SDL_AudioStream* audioStream_ = nullptr;
    std::atomic<bool> shuttingDown_{false};
};

/**
 * Decode audio file data (WAV, MP3, OGG, etc.)
 * Returns nullptr on failure.
 */
std::shared_ptr<AudioBuffer> decodeAudioFile(const uint8_t* data, size_t length, float targetSampleRate);

}  // namespace audio
}  // namespace mystral

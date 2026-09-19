/**
 * Web Audio API Implementation using SDL3
 */

#include "mystral/audio/audio_context.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace mystral {
namespace audio {

// ============================================================================
// AudioBuffer
// ============================================================================

AudioBuffer::AudioBuffer(float sampleRate, int numberOfChannels, size_t length)
    : sampleRate_(sampleRate)
    , numberOfChannels_(numberOfChannels)
    , length_(length) {
    channelData_.resize(numberOfChannels);
    for (int i = 0; i < numberOfChannels; i++) {
        channelData_[i].resize(length, 0.0f);
    }
}

AudioBuffer::~AudioBuffer() = default;

float* AudioBuffer::getChannelData(int channel) {
    if (channel < 0 || channel >= numberOfChannels_) return nullptr;
    return channelData_[channel].data();
}

const float* AudioBuffer::getChannelData(int channel) const {
    if (channel < 0 || channel >= numberOfChannels_) return nullptr;
    return channelData_[channel].data();
}

void AudioBuffer::setFromInterleaved(const float* data, size_t numSamples, int numChannels) {
    size_t frames = numSamples / numChannels;
    length_ = frames;
    numberOfChannels_ = numChannels;
    channelData_.resize(numChannels);

    for (int ch = 0; ch < numChannels; ch++) {
        channelData_[ch].resize(frames);
        for (size_t i = 0; i < frames; i++) {
            channelData_[ch][i] = data[i * numChannels + ch];
        }
    }
}

// ============================================================================
// AudioParam
// ============================================================================

AudioParam::AudioParam(float defaultValue)
    : value_(defaultValue)
    , defaultValue_(defaultValue) {}

// ============================================================================
// AudioNode
// ============================================================================

AudioNode::AudioNode(AudioContext* context)
    : context_(context) {}

void AudioNode::connect(AudioNode* destination) {
    outputs_.push_back(destination);
}

void AudioNode::disconnect() {
    outputs_.clear();
}

// ============================================================================
// AudioDestinationNode
// ============================================================================

AudioDestinationNode::AudioDestinationNode(AudioContext* context)
    : AudioNode(context) {}

// ============================================================================
// GainNode
// ============================================================================

GainNode::GainNode(AudioContext* context)
    : AudioNode(context)
    , gain_(1.0f) {}

void GainNode::process(float* output, size_t numFrames, int numChannels) {
    float gainValue = gain_.value();
    for (size_t i = 0; i < numFrames * numChannels; i++) {
        output[i] *= gainValue;
    }
}

// ============================================================================
// AudioBufferSourceNode
// ============================================================================

AudioBufferSourceNode::AudioBufferSourceNode(AudioContext* context)
    : AudioNode(context) {}

AudioBufferSourceNode::~AudioBufferSourceNode() {
    context_->unregisterSource(this);
}

void AudioBufferSourceNode::setBuffer(std::shared_ptr<AudioBuffer> buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_ = buffer;
}

void AudioBufferSourceNode::start(double when, double offset, double duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (isPlaying_ || !buffer_) return;

    startTime_ = context_->currentTime() + when;
    offsetTime_ = offset;
    durationTime_ = duration;
    playbackPosition_ = static_cast<size_t>(offset * buffer_->sampleRate());
    isPlaying_ = true;

    lock.unlock();
    context_->registerSource(this);
}

void AudioBufferSourceNode::stop(double when) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isPlaying_) return;
    stopTime_ = context_->currentTime() + when;
}

void AudioBufferSourceNode::process(float* output, size_t numFrames, int numChannels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isPlaying_ || !buffer_) return;

    double currentTime = context_->currentTime();

    // Check if we should stop
    if (stopTime_ >= 0 && currentTime >= stopTime_) {
        isPlaying_ = false;
        if (onended) onended();
        return;
    }

    // Check if we should start yet
    if (currentTime < startTime_) {
        return;
    }

    int bufferChannels = buffer_->numberOfChannels();
    size_t bufferLength = buffer_->length();

    if (bufferLength == 0) {
        isPlaying_ = false;
        return;
    }
    const size_t loopStartSample = static_cast<size_t>(std::clamp(loopStart_, 0.0, buffer_->duration()) * buffer_->sampleRate());
    size_t loopEndSample = loopEnd_ > 0
                               ? static_cast<size_t>(std::min(loopEnd_, buffer_->duration()) * buffer_->sampleRate())
                               : bufferLength;
    const bool validLoop = loopStartSample < loopEndSample;
    for (size_t frame = 0; frame < numFrames; frame++) {
        if (playbackPosition_ >= (loop_ && validLoop ? loopEndSample : bufferLength)) {
            if (loop_) {
                playbackPosition_ = validLoop ? loopStartSample : 0;
            } else {
                // End of buffer
                isPlaying_ = false;
                if (onended) onended();
                return;
            }
        }

        // Check duration limit
        if (durationTime_ > 0) {
            double playedTime = static_cast<double>(playbackPosition_) / buffer_->sampleRate() - offsetTime_;
            if (playedTime >= durationTime_) {
                isPlaying_ = false;
                if (onended) onended();
                return;
            }
        }

        // Mix audio into output
        for (int ch = 0; ch < numChannels; ch++) {
            int srcChannel = ch % bufferChannels;
            const float* channelData = buffer_->getChannelData(srcChannel);
            if (channelData) {
                output[frame * numChannels + ch] += channelData[playbackPosition_];
            }
        }

        playbackPosition_++;
    }
}

// ============================================================================
// AudioContext
// ============================================================================

AudioContext::AudioContext() {
    destination_ = std::make_unique<AudioDestinationNode>(this);

    // Initialize SDL audio
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            std::cerr << "[Audio] Failed to init SDL audio: " << SDL_GetError() << std::endl;
            return;
        }
    }

    // Create audio stream
    SDL_AudioSpec spec;
    spec.freq = static_cast<int>(sampleRate_);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;

    audioStream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        sdlAudioCallback,
        this
    );

    if (!audioStream_) {
        std::cerr << "[Audio] Failed to open audio device: " << SDL_GetError() << std::endl;
        return;
    }

    std::cout << "[Audio] AudioContext created (sample rate: " << sampleRate_ << " Hz)" << std::endl;
}

AudioContext::~AudioContext() {
    close();
}

double AudioContext::currentTime() const {
    return static_cast<double>(sampleCount_.load()) / sampleRate_;
}

std::shared_ptr<AudioBuffer> AudioContext::createBuffer(int numberOfChannels, size_t length, float sampleRate) {
    return std::make_shared<AudioBuffer>(sampleRate, numberOfChannels, length);
}

std::unique_ptr<AudioBufferSourceNode> AudioContext::createBufferSource() {
    return std::make_unique<AudioBufferSourceNode>(this);
}

std::unique_ptr<GainNode> AudioContext::createGain() {
    return std::make_unique<GainNode>(this);
}

std::shared_ptr<AudioBuffer> AudioContext::decodeAudioDataSync(const uint8_t* data, size_t length) {
    return decodeAudioFile(data, length, sampleRate_);
}

void AudioContext::resume() {
    if (state_ == State::Closed) return;
    if (audioStream_) {
        SDL_ResumeAudioStreamDevice(audioStream_);
    }
    state_ = State::Running;
    std::cout << "[Audio] AudioContext resumed" << std::endl;
}

void AudioContext::suspend() {
    if (state_ == State::Closed) return;
    if (audioStream_) {
        SDL_PauseAudioStreamDevice(audioStream_);
    }
    state_ = State::Suspended;
}

void AudioContext::close() {
    if (state_ == State::Closed) return;

    // Signal callback to stop processing first
    shuttingDown_.store(true, std::memory_order_release);

    if (audioStream_) {
        // Destroy the audio stream - SDL will wait for callbacks to finish
        SDL_DestroyAudioStream(audioStream_);
        audioStream_ = nullptr;
    }

    state_ = State::Closed;
}

void AudioContext::registerSource(AudioBufferSourceNode* source) {
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    activeSources_.push_back(source);
    std::cout << "[Audio] Source registered, active sources: " << activeSources_.size() << std::endl;
}

void AudioContext::unregisterSource(AudioBufferSourceNode* source) {
    std::lock_guard<std::mutex> lock(sourcesMutex_);
    activeSources_.erase(
        std::remove(activeSources_.begin(), activeSources_.end(), source),
        activeSources_.end()
    );
}

void AudioContext::audioCallback(float* output, int numFrames) {
    // Clear output buffer
    std::memset(output, 0, numFrames * 2 * sizeof(float));

    // Mix all active sources
    {
        std::lock_guard<std::mutex> lock(sourcesMutex_);
        for (auto* source : activeSources_) {
            source->process(output, numFrames, 2);
        }
        activeSources_.erase(std::remove_if(activeSources_.begin(), activeSources_.end(),
                                            [](AudioBufferSourceNode* source) { return !source->isPlaying(); }),
                             activeSources_.end());
    }

    // Clamp output to [-1, 1]
    for (int i = 0; i < numFrames * 2; i++) {
        output[i] = std::clamp(output[i], -1.0f, 1.0f);
    }

    sampleCount_ += numFrames;
}

static int g_callbackCount = 0;

// Static buffer for audio callback to avoid allocation on audio thread
static float s_audioBuffer[8192];  // ~93ms at 44.1kHz stereo

void AudioContext::sdlAudioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount) {
    // Safety check: validate userdata pointer first
    if (!userdata || !stream) {
        return;
    }

    // SDL3 callback: we need to provide audio data to the stream
    // additionalAmount is the minimum bytes needed
    if (additionalAmount <= 0) return;

    auto* ctx = static_cast<AudioContext*>(userdata);

    // Check if we're shutting down - return silence immediately
    // Note: Don't do any I/O (cout) in callbacks - can cause hangs
    if (ctx->shuttingDown_.load(std::memory_order_relaxed)) {
        // Put silence to satisfy the callback - use static buffer
        std::memset(s_audioBuffer, 0, additionalAmount);
        SDL_PutAudioStreamData(stream, s_audioBuffer, additionalAmount);
        return;
    }

    // Only log first few callbacks (no I/O in callback itself)
    g_callbackCount++;

    int numFrames = additionalAmount / (2 * sizeof(float));  // Stereo float

    // Safety: limit numFrames to static buffer size
    if (numFrames <= 0 || numFrames > 4096) {
        numFrames = std::min(numFrames, 4096);
        if (numFrames <= 0) return;
    }

    // Use static buffer to avoid allocation
    ctx->audioCallback(s_audioBuffer, numFrames);

    // Put audio data into the stream
    SDL_PutAudioStreamData(stream, s_audioBuffer, numFrames * 2 * sizeof(float));
}

// ============================================================================
// Audio Decoding
// ============================================================================

std::shared_ptr<AudioBuffer> decodeAudioFile(const uint8_t* data, size_t length, float targetSampleRate) {
    // Use SDL to load audio data
    SDL_IOStream* io = SDL_IOFromConstMem(data, length);
    if (!io) {
        std::cerr << "[Audio] Failed to create IO stream" << std::endl;
        return nullptr;
    }

    SDL_AudioSpec spec;
    uint8_t* audioData = nullptr;
    uint32_t audioLen = 0;

    if (!SDL_LoadWAV_IO(io, true, &spec, &audioData, &audioLen)) {
        std::cerr << "[Audio] Failed to load audio: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    // Convert to float if necessary
    std::vector<float> floatData;
    int numChannels = spec.channels;
    size_t numSamples = 0;

    if (spec.format == SDL_AUDIO_F32) {
        numSamples = audioLen / sizeof(float);
        floatData.resize(numSamples);
        std::memcpy(floatData.data(), audioData, audioLen);
    } else if (spec.format == SDL_AUDIO_S16) {
        numSamples = audioLen / sizeof(int16_t);
        floatData.resize(numSamples);
        const int16_t* src = reinterpret_cast<const int16_t*>(audioData);
        for (size_t i = 0; i < numSamples; i++) {
            floatData[i] = src[i] / 32768.0f;
        }
    } else if (spec.format == SDL_AUDIO_U8) {
        numSamples = audioLen;
        floatData.resize(numSamples);
        for (size_t i = 0; i < numSamples; i++) {
            floatData[i] = (audioData[i] - 128) / 128.0f;
        }
    } else {
        std::cerr << "[Audio] Unsupported audio format: " << spec.format << std::endl;
        SDL_free(audioData);
        return nullptr;
    }

    SDL_free(audioData);

    // Create AudioBuffer
    size_t numFrames = numSamples / numChannels;
    auto buffer = std::make_shared<AudioBuffer>(static_cast<float>(spec.freq), numChannels, numFrames);
    buffer->setFromInterleaved(floatData.data(), numSamples, numChannels);

    std::cout << "[Audio] Decoded audio: " << numFrames << " frames, "
              << numChannels << " channels, " << spec.freq << " Hz" << std::endl;

    return buffer;
}

}  // namespace audio
}  // namespace mystral

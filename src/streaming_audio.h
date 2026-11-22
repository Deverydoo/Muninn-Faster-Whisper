#pragma once

// Audio input for StreamingTranscriber: channel downmix, a resampler that
// keeps its phase between calls, the shared audio buffer and a capped queue.
// Internal to Muninn, and header-only so tests/test_streaming.cpp can check
// exactly the code the transcriber runs.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace muninn {
namespace streaming_detail {

// Averages interleaved frames down to mono. `sample_count` is the number of
// floats in `samples` (frames x channels); a trailing partial frame is
// ignored. Throws std::invalid_argument for channels < 1.
inline void downmix_to_mono(const float* samples, size_t sample_count, int channels,
                            std::vector<float>& out) {
    if (channels < 1) {
        throw std::invalid_argument("push_audio: channels must be 1 or more");
    }
    const size_t frames = sample_count / static_cast<size_t>(channels);
    out.resize(frames);
    if (channels == 1) {
        for (size_t i = 0; i < frames; ++i) out[i] = samples[i];
        return;
    }
    const float scale = 1.0f / static_cast<float>(channels);
    for (size_t f = 0; f < frames; ++f) {
        const float* frame = samples + f * static_cast<size_t>(channels);
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) sum += frame[c];
        out[f] = sum * scale;
    }
}

// Linear-interpolation resampler that carries its position from one call to
// the next, so audio pushed in many small blocks comes out exactly as if it
// had been pushed in one piece: no drift, no clicks at block edges.
//
// The read position is kept as an exact fraction (numerator over the output
// rate), so it never accumulates rounding error, even over hours.
//
// There is no low-pass filter before downsampling, so content above the new
// Nyquist frequency (8 kHz for 16 kHz output) folds back. Speech sits well
// below that and Whisper copes; a polyphase filter would be the next step if
// it ever matters.
class LinearResampler {
public:
    LinearResampler() = default;

    // Changing either rate restarts the phase (the previous sample is kept).
    void set_rates(int in_rate, int out_rate) {
        if (in_rate <= 0 || out_rate <= 0) {
            throw std::invalid_argument("resampler: sample rates must be positive");
        }
        if (in_rate != in_rate_ || out_rate != out_rate_) {
            in_rate_ = in_rate;
            out_rate_ = out_rate;
            pos_num_ = 0;
        }
    }

    void reset() {
        pos_num_ = 0;
        have_prev_ = false;
        prev_ = 0.0f;
    }

    int in_rate() const { return in_rate_; }
    int out_rate() const { return out_rate_; }

    // Appends the resampled block to `out`.
    void process(const float* in, size_t n, std::vector<float>& out) {
        if (n == 0) return;
        if (in_rate_ == out_rate_) {
            out.insert(out.end(), in, in + n);
            prev_ = in[n - 1];
            have_prev_ = true;
            return;
        }
        // Output k sits at input position pos_num_ / out_rate_, counted from
        // this block's first sample; -1 is the previous block's last sample.
        const int64_t out_rate = out_rate_;
        const int64_t last = static_cast<int64_t>(n - 1) * out_rate;
        if (!have_prev_ && pos_num_ < 0) pos_num_ = 0;  // nothing before the very first sample
        while (pos_num_ <= last) {
            const int64_t whole = floor_div(pos_num_, out_rate);
            const int64_t rem = pos_num_ - whole * out_rate;
            const float a = (whole < 0) ? prev_ : in[whole];
            float value = a;
            if (rem != 0) {
                const float b = in[whole + 1];
                value = a + (b - a) * (static_cast<float>(rem) / static_cast<float>(out_rate));
            }
            out.push_back(value);
            pos_num_ += in_rate_;
        }
        pos_num_ -= static_cast<int64_t>(n) * out_rate;
        prev_ = in[n - 1];
        have_prev_ = true;
    }

private:
    static int64_t floor_div(int64_t a, int64_t b) {
        int64_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    int in_rate_ = 16000;
    int out_rate_ = 16000;
    int64_t pos_num_ = 0;
    float prev_ = 0.0f;
    bool have_prev_ = false;
};

// The audio waiting to be transcribed, shared between push_audio (the
// producer) and the worker (the consumer). Every call takes the buffer's own
// lock for a copy at most, never for a decode.
//
// Positions are absolute sample indices since the session started, so the
// worker can time segments correctly however far behind it is. When the
// buffer is full the oldest audio is dropped (and counted); clear() starts a
// new generation so a worker holding a copy from before can tell its result is
// stale.
class AudioBuffer {
public:
    explicit AudioBuffer(size_t capacity = 16000 * 60) : capacity_(capacity ? capacity : 1) {}

    // Drops the content and moves the timeline back to 0.
    void reset(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = capacity ? capacity : 1;
        data_.clear();
        front_ = 0;
        dropped_ = 0;
        ++generation_;
    }

    void clear() { reset(capacity()); }

    // Appends samples; returns how many old samples were dropped to make room.
    size_t push(const float* samples, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.insert(data_.end(), samples, samples + n);
        size_t dropped = 0;
        if (data_.size() > capacity_) {
            dropped = data_.size() - capacity_;
            data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(dropped));
            front_ += dropped;
            dropped_ += dropped;
        }
        return dropped;
    }

    struct Snapshot {
        uint64_t front = 0;       // absolute index of the first sample buffered
        size_t available = 0;     // samples buffered
        uint64_t generation = 0;
    };

    Snapshot peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {front_, data_.size(), generation_};
    }

    // Copies up to `max` samples from the front. Returns the snapshot the copy
    // was taken from.
    Snapshot copy_front(size_t max, std::vector<float>& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t n = max < data_.size() ? max : data_.size();
        out.assign(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(n));
        return {front_, data_.size(), generation_};
    }

    // Removes everything before absolute index `position`. Ignored when the
    // buffer was cleared since `generation` was read; positions already
    // dropped are fine.
    void consume_to(uint64_t position, uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || position <= front_) return;
        const uint64_t end = front_ + data_.size();
        if (position > end) position = end;
        const size_t n = static_cast<size_t>(position - front_);
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(n));
        front_ = position;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    size_t capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<float> data_;
    size_t capacity_;
    uint64_t front_ = 0;
    uint64_t dropped_ = 0;
    uint64_t generation_ = 0;
};

// A FIFO that keeps at most `cap` items: pushing onto a full queue drops the
// oldest. Not locked; the owner locks.
template <typename T>
class CappedQueue {
public:
    explicit CappedQueue(size_t cap = 1000) : cap_(cap ? cap : 1) {}

    void set_cap(size_t cap) {
        cap_ = cap ? cap : 1;
        while (items_.size() > cap_) {
            items_.pop_front();
            ++dropped_;
        }
    }

    // Returns true when an old item was dropped to make room.
    bool push(T item) {
        bool dropped = false;
        if (items_.size() >= cap_) {
            items_.pop_front();
            ++dropped_;
            dropped = true;
        }
        items_.push_back(std::move(item));
        return dropped;
    }

    bool pop(T& out) {
        if (items_.empty()) return false;
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    void clear() {
        items_.clear();
        dropped_ = 0;
    }

    size_t size() const { return items_.size(); }
    size_t cap() const { return cap_; }
    uint64_t dropped() const { return dropped_; }

private:
    std::deque<T> items_;
    size_t cap_;
    uint64_t dropped_ = 0;
};

}  // namespace streaming_detail
}  // namespace muninn

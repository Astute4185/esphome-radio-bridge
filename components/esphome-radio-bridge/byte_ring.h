#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::serial_radio_gateway {
// A bounded, allocation-free SPSC buffer; calls are made from the ESPHome loop.
template <size_t N> class ByteRing {
 public:
  static_assert(N > 0);
  size_t size() const { return size_; }
  size_t free() const { return N - size_; }
  bool empty() const { return size_ == 0; }
  void clear() { head_ = tail_ = size_ = 0; }

  // Returns bytes stored; callers must provide transport backpressure for the rest.
  size_t push(const uint8_t *input, size_t count) {
    size_t done = 0;
    while (done < count && size_ < N) {
      size_t chunk = count - done;
      if (chunk > N - tail_)
        chunk = N - tail_;
      if (chunk > N - size_)
        chunk = N - size_;
      std::memcpy(data_ + tail_, input + done, chunk);
      tail_ = (tail_ + chunk) % N;
      size_ += chunk;
      done += chunk;
    }
    return done;
  }

  // Contiguous pending region, valid until the next mutating call.
  const uint8_t *head_data() const { return data_ + head_; }
  size_t head_size() const { return size_ < N - head_ ? size_ : N - head_; }
  void consume(size_t count) {
    if (count > size_)
      count = size_;
    head_ = (head_ + count) % N;
    size_ -= count;
  }

 private:
  uint8_t data_[N]{};
  size_t head_{0};
  size_t tail_{0};
  size_t size_{0};
};
}  // namespace esphome::serial_radio_gateway

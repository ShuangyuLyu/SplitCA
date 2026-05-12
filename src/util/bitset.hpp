#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

class BitSet {
 public:
    BitSet() : bitmap_(nullptr), capacity_(0) {}
    explicit BitSet(size_t size) {
        capacity_ = (size + 63) >> 6;
        bitmap_ = new uint64_t[capacity_]{0};
    }
    ~BitSet() { delete[] bitmap_; }

    BitSet(const BitSet &bs) : capacity_(bs.capacity_) {
        bitmap_ = new uint64_t[bs.capacity_]{0};
        memcpy(bitmap_, bs.bitmap_, sizeof(uint64_t) * capacity_);
    }
    BitSet(BitSet &&bs) noexcept : capacity_(bs.capacity_) {
        bitmap_ = bs.bitmap_;
        bs.bitmap_ = nullptr;
    }

    BitSet &operator=(const BitSet &bs) {
        if (this == &bs) return *this;
        delete[] bitmap_;
        capacity_ = bs.capacity_;
        bitmap_ = new uint64_t[capacity_]{0};
        memcpy(bitmap_, bs.bitmap_, sizeof(uint64_t) * capacity_);
        return *this;
    }
    BitSet &operator=(BitSet &&bs) noexcept {
        if (this != &bs) swap(bs);
        return *this;
    }

    void swap(BitSet &bs) noexcept {
        std::swap(bitmap_, bs.bitmap_);
        std::swap(capacity_, bs.capacity_);
    }
    friend void swap(BitSet &a, BitSet &b) noexcept { a.swap(b); }

    BitSet &operator|=(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] |= bs.bitmap_[i];
        return *this;
    }
    BitSet &operator&=(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] &= bs.bitmap_[i];
        return *this;
    }
    BitSet &operator^=(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] ^= bs.bitmap_[i];
        return *this;
    }

    /// *this |= ~bs
    BitSet &or_not(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] |= ~bs.bitmap_[i];
        return *this;
    }
    /// *this &= ~bs
    BitSet &and_not(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] &= ~bs.bitmap_[i];
        return *this;
    }
    /// *this ^= ~bs
    BitSet &xor_not(const BitSet &bs) {
        int len = std::min(capacity_, bs.capacity_);
        for (int i = 0; i < len; i++) bitmap_[i] ^= ~bs.bitmap_[i];
        return *this;
    }

    class BitRef {
        friend BitSet;
        BitRef(uint64_t *ptr, unsigned idx) : ptr_(ptr), idx_(idx) {}
        BitRef(const BitRef &) = delete;
        BitRef(BitRef &&) = default;
        BitRef &operator=(const BitRef &) = delete;
        BitRef &operator=(BitRef &&) = default;

     public:
        operator bool() const { return (*ptr_ >> idx_) & 1; }
        BitRef &operator=(bool value) {
            value ? (*ptr_ |= uint64_t{1} << idx_) : (*ptr_ &= ~(uint64_t{1} << idx_));
            return *this;
        }

     private:
        uint64_t *ptr_;
        unsigned idx_;
    };

    BitRef operator[](size_t idx) { return BitRef(bitmap_ + (idx >> 6), idx & 63); }
    bool operator[](size_t idx) const { return (bitmap_[idx >> 6] >> (idx & 63)) & 1; }
    void set(size_t idx) { (*this)[idx] = true; }
    void unset(size_t idx) { (*this)[idx] = false; }
    bool get(size_t idx) const { return (*this)[idx]; }

    int popcount() const {
        int cnt = 0;
        for (int i = 0; i < capacity_; i++) cnt += __builtin_popcountll(bitmap_[i]);
        return cnt;
    }

    std::string to_string() const {
        std::string result;
        for (size_t i = 0; i < capacity_; i++) {
            for (unsigned j = 0; j < 64; j++) {
                result += get((i << 6) + j) ? '1' : '0';
            }
        }
        return result;
    }

 private:
    uint64_t *bitmap_;
    size_t capacity_;
};

template <>
inline void std::swap(BitSet &a, BitSet &b) noexcept {
    a.swap(b);
}
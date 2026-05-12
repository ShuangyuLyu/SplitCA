#pragma once
#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <span>
#include <vector>

inline constexpr int mx_strength = 4;

struct t_tuple {
    int v[mx_strength];
    t_tuple() {}
    t_tuple(int strength) {}
    t_tuple(const std::vector<int> &vec) {
        for (int i = 0; i < (int)vec.size(); i++) v[i] = vec[i];
    }
    t_tuple(std::initializer_list<int> il) {
        for (int i = 0; int x : il) v[i++] = x;
    }
    void print(int strength) {
        std::cout << "(";
        for (int i = 0; i < strength; i++) std::cout << v[i] << ", ";
        std::cout << ")\n";
    }

    template <size_t N>
        requires (1 <= N && N <= mx_strength)
    operator std::array<int, N> const &() const {
        return reinterpret_cast<std::array<int, N> const &>(*this);
    }
};

// Save some memory...
// Note: the reference type of TupleVector is `const t_tuple &`, but it's a mock object, only first
// `strength` values are availiable.
class TupleVector {
 public:
    class const_iterator {
        friend TupleVector;
        using inner_iter_t = std::vector<int>::const_iterator;
        inner_iter_t it_{};
        size_t strength_;

     public:
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = t_tuple;
        using pointer_type = const t_tuple *;
        using reference_type = const t_tuple &;
        using difference_type = inner_iter_t::difference_type;

        const_iterator() = default;
        const_iterator(inner_iter_t it, size_t strength) : it_(it), strength_(strength) {}
        reference_type operator*() const { return reinterpret_cast<reference_type>(*it_); }
        pointer_type operator->() const { return reinterpret_cast<pointer_type>(&*it_); }
        reference_type operator[](difference_type n) const { return *(*this + n); }
        const_iterator &operator++() { return it_ += strength_, *this; }
        const_iterator operator++(int) {
            auto self = *this;
            ++*this;
            return self;
        }
        const_iterator &operator--() { return it_ -= strength_, *this; }
        const_iterator operator--(int) {
            auto self = *this;
            --*this;
            return self;
        }
        const_iterator &operator+=(difference_type n) { return it_ += n * strength_, *this; }
        const_iterator &operator-=(difference_type n) { return it_ -= n * strength_, *this; }
        friend const_iterator operator+(const const_iterator &i, difference_type n) {
            return {i.it_ + n * i.strength_, i.strength_};
        }
        friend const_iterator operator+(difference_type n, const const_iterator &i) {
            return i + n;
        }
        friend const_iterator operator-(const const_iterator &i, difference_type n) {
            return {i.it_ - n * i.strength_, i.strength_};
        }
        friend difference_type operator-(const const_iterator &i, const const_iterator &j) {
            assert(i.strength_ == j.strength_);
            return (i.it_ - j.it_) / i.strength_;
        }
        friend bool operator==(const const_iterator &, const const_iterator &) = default;
        friend auto operator<=>(const const_iterator &, const const_iterator &) = default;
    };
    using reference_type = const_iterator::reference_type;
    using value_type = const_iterator::value_type;
    using diffrence_type = const_iterator::difference_type;

    TupleVector() noexcept = default;
    ~TupleVector() noexcept = default;
    TupleVector(TupleVector &&) noexcept = default;
    TupleVector(TupleVector const &) noexcept = default;
    TupleVector &operator=(TupleVector &&) noexcept = default;
    TupleVector &operator=(TupleVector const &) noexcept = default;

    explicit TupleVector(size_t strength) noexcept : strength_(strength) {}
    TupleVector(const_iterator begin, const_iterator end) : strength_(begin.strength_) {
        extends(begin, end);
    }

    void set_strength(size_t size) noexcept { strength_ = size; }
    size_t get_strength() const noexcept { return strength_; }

    void reserve(size_t new_size) { data_.reserve(new_size * strength_); }
    bool empty() const noexcept { return data_.empty(); }
    size_t size() const noexcept { return data_.size() / strength_; }
    size_t capacity() const noexcept { return data_.capacity() / strength_; }
    void shrink_to_fit() { data_.shrink_to_fit(); }

    void push_back(const t_tuple &tuple) {
        assert(strength_ > 0 && strength_ <= mx_strength);
        data_.insert(data_.cend(), tuple.v, tuple.v + strength_);
    }
    void pop_back() { data_.resize(data_.size() - strength_); }
    void clear() { data_.clear(); }
    void resize(size_t new_size, const t_tuple &tuple) {
        if (size() >= new_size) {
            data_.resize(new_size * strength_);
            return;
        }
        data_.reserve(new_size * strength_);
        while (size() < new_size) push_back(tuple);
    }

    void extends(std::span<const int> other) {
        size_t old_size = data_.size();
        data_.resize(old_size + other.size());
        std::memcpy(data_.data() + old_size, other.data(), other.size() * sizeof(int));
    }
    void extends(const TupleVector &other) { extends(other.data_); }
    void extends(const_iterator begin, const_iterator end) {
        extends(std::span(begin.it_, end.it_));
    }

    reference_type operator[](size_t index) const { return begin()[index]; }
    reference_type front() const { return *begin(); }
    reference_type back() const { return end()[-1]; }

    const_iterator begin() const { return const_iterator(data_.begin(), strength_); }
    const_iterator end() const { return const_iterator(data_.end(), strength_); }

 private:
    std::vector<int> data_;
    size_t strength_ = 0;
};

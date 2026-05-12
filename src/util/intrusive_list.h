#pragma once
#include <algorithm>
#include <cstring>
#include <stack>
#include <utility>

template <class T, size_t block_size = 64 * 1024 * 1024>
class large_unsynchronized_pool_allocator {
    void *current_allocated_ptr = nullptr;
    size_t current_alloacted_unused = 0;
    std::stack<void *> available_stack;
    std::stack<void *> allocated_ever;

 public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::false_type;

    large_unsynchronized_pool_allocator() = default;
    ~large_unsynchronized_pool_allocator() { release_all(); }
    large_unsynchronized_pool_allocator(const large_unsynchronized_pool_allocator &) = delete;
    large_unsynchronized_pool_allocator(large_unsynchronized_pool_allocator &&other) noexcept
        : current_allocated_ptr(std::exchange(other.current_allocated_ptr, nullptr)),
          current_alloacted_unused(std::exchange(other.current_alloacted_unused, 0)),
          available_stack(std::exchange(other.available_stack, {})),
          allocated_ever(std::exchange(other.allocated_ever, {})) {}
    auto &operator=(const large_unsynchronized_pool_allocator &) = delete;
    auto &operator=(large_unsynchronized_pool_allocator &&other) noexcept {
        if (this == std::addressof(other)) return *this;
        std::swap(current_allocated_ptr, other.current_allocated_ptr);
        std::swap(current_alloacted_unused, other.current_alloacted_unused);
        std::swap(available_stack, other.available_stack);
        std::swap(allocated_ever, other.allocated_ever);
        return *this;
    }

    void release_all() {
        while (!allocated_ever.empty()) {
            void *top = allocated_ever.top();
            free(top);
            allocated_ever.pop();
        }
        std::stack<void *>{}.swap(available_stack);
        current_allocated_ptr = nullptr;
        current_alloacted_unused = 0;
    }

    void *allocate() {
        if (!available_stack.empty()) {
            void *ptr = available_stack.top();
            available_stack.pop();
            return ptr;
        }
        if (!current_alloacted_unused) {
            current_allocated_ptr = malloc(sizeof(T) * block_size);
            allocated_ever.push(current_allocated_ptr);
            current_alloacted_unused = block_size;
        }
        void *ptr = current_allocated_ptr;
        current_allocated_ptr = static_cast<T *>(current_allocated_ptr) + 1;
        current_alloacted_unused -= 1;
        return ptr;
    }
    void deallocate(void *ptr) { available_stack.push(ptr); }
};

struct IntrusiveListNode {
    int val;
    IntrusiveListNode *pre, *nxt;
    IntrusiveListNode() { pre = nxt = NULL; }
    IntrusiveListNode(int x) {
        val = x;
        pre = nxt = NULL;
    }
};

// Use note: IntrusiveList use a user-defined allocator.
// Methods `insert`, `erase` and `clear` need a allocator instance as a parameter.
// Note that it is undefined behaviour if deferrent allocator instances are used in one list.
// IntrusiveList intentionally leak the memory when deinit.
class IntrusiveList {
 public:
    using Allocator = large_unsynchronized_pool_allocator<IntrusiveListNode>;

    IntrusiveList() = default;
    ~IntrusiveList() = default;

    IntrusiveList(const IntrusiveList &) = delete;
    IntrusiveList(IntrusiveList &&other) noexcept
        : head(std::exchange(other.head, nullptr)),
          tail(std::exchange(other.tail, nullptr)),
          size_(std::exchange(other.size_, 0)) {}
    IntrusiveList &operator=(const IntrusiveList &) = delete;
    IntrusiveList &operator=(IntrusiveList &&other) noexcept {
        if (this == &other) return *this;
        std::swap(other.head, head);
        std::swap(other.tail, tail);
        std::swap(other.size_, size_);
        return *this;
    }

    void insert(int x, Allocator &al) {
        void *ptr = al.allocate();
        IntrusiveListNode *t = new (ptr) IntrusiveListNode;
        t->val = x, t->pre = tail, t->nxt = NULL;
        if (head == NULL)
            head = tail = t;
        else
            tail->nxt = t, tail = t;
        size_++;
    }

    void erase(IntrusiveListNode *t, Allocator &al) {
        IntrusiveListNode *pre = t->pre, *nxt = t->nxt;
        if (size_ == 1)
            head = tail = NULL;
        else if (t == head)
            head = t->nxt;
        else if (t == tail)
            pre->nxt = NULL, tail = pre;
        else
            pre->nxt = nxt, nxt->pre = pre;

        t->pre = t->nxt = NULL;
        al.deallocate(t);
        size_--;
    }

    int size() { return size_; }

    int &front() { return head->val; }

    void clear(Allocator &al) {
        for (IntrusiveListNode *p = head, *q; p != NULL; p = q) {
            q = p->nxt;
            al.deallocate(p);
        }
        head = tail = NULL, size_ = 0;
    }

    IntrusiveListNode *head = nullptr, *tail = nullptr;

 private:
    int size_ = 0;
};

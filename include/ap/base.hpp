#pragma once

#include <stddef.h>
#include <stdint.h>

// The runtime deliberately needs no libstdc++, exceptions, RTTI or POSIX shim.
extern "C" void* ap_allocate(size_t bytes);
extern "C" void ap_deallocate(void* pointer);
extern "C" [[noreturn]] void ap_panic(const char* message);
extern "C" void* ap_memcpy(void* to, const void* from, size_t size);
extern "C" void* ap_memmove(void* to, const void* from, size_t size);
extern "C" void* ap_memset(void* to, int value, size_t size);

struct ApPlacement {};
inline void* operator new(size_t, ApPlacement, void* pointer) noexcept {
    return pointer;
}
inline void operator delete(void*, ApPlacement, void*) noexcept {}

namespace ap {
template <class T> T&& move(T& value) {
    return static_cast<T&&>(value);
}
inline size_t length(const char* text) {
    size_t n = 0;
    if (text)
        while (text[n])
            ++n;
    return n;
}
inline bool equal(const char* a, const char* b) {
    if (!a || !b)
        return a == b;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

class Str {
    char* data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;

  public:
    Str() = default;
    Str(const char* text) : Str(text, length(text)) {}
    Str(const char* text, size_t size) {
        append(text, size);
    }
    Str(const Str& other) : Str(other.c_str(), other.size_) {}
    Str(Str&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }
    ~Str() {
        ap_deallocate(data_);
    }
    Str& operator=(Str other) noexcept {
        swap(other);
        return *this;
    }
    void swap(Str& other) noexcept {
        char* p = data_;
        data_ = other.data_;
        other.data_ = p;
        size_t n = size_;
        size_ = other.size_;
        other.size_ = n;
        n = capacity_;
        capacity_ = other.capacity_;
        other.capacity_ = n;
    }
    const char* c_str() const {
        return data_ ? data_ : "";
    }
    char* data() {
        reserve(size_ + 1);
        return data_;
    }
    size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    char operator[](size_t index) const {
        return c_str()[index];
    }
    void reserve(size_t capacity) {
        if (capacity <= capacity_)
            return;
        if (capacity > size_t(-1) / 2)
            ap_panic("String is too large");
        size_t next = capacity_ ? capacity_ * 2 : 32;
        if (next < capacity)
            next = capacity;
        char* p = static_cast<char*>(ap_allocate(next));
        if (!p)
            ap_panic("Out of memory");
        if (size_) {
            ap_memcpy(p, c_str(), size_);
        }
        p[size_] = 0;
        ap_deallocate(data_);
        data_ = p;
        capacity_ = next;
    }
    void append(const char* text, size_t size) {
        if (!size)
            return;
        if (size > size_t(-1) - size_ - 1)
            ap_panic("String overflow");
        uintptr_t source = reinterpret_cast<uintptr_t>(text), begin = reinterpret_cast<uintptr_t>(data_);
        bool aliased = data_ && source >= begin && source - begin <= size_;
        size_t offset = aliased ? size_t(source - begin) : 0;
        reserve(size_ + size + 1);
        ap_memcpy(data_ + size_, aliased ? data_ + offset : text, size);
        size_ += size;
        data_[size_] = 0;
    }
    void append(const Str& value) {
        append(value.c_str(), value.size());
    }
    void append(const char* value) {
        append(value, length(value));
    }
    void push(char value) {
        append(&value, 1);
    }
    void resize(size_t size) {
        if (size == size_t(-1))
            ap_panic("String overflow");
        reserve(size + 1);
        if (size > size_)
            ap_memset(data_ + size_, 0, size - size_);
        size_ = size;
        data_[size] = 0;
    }
    void clear() {
        size_ = 0;
        if (data_)
            data_[0] = 0;
    }
    size_t find(char ch, size_t start = 0) const {
        for (size_t i = start; i < size_; ++i)
            if ((*this)[i] == ch)
                return i;
        return size_t(-1);
    }
    size_t find(const Str& value, size_t start = 0) const {
        if (value.size() > size_)
            return size_t(-1);
        for (size_t i = start; i <= size_ - value.size(); ++i) {
            size_t j = 0;
            while (j < value.size() && (*this)[i + j] == value[j])
                ++j;
            if (j == value.size())
                return i;
        }
        return size_t(-1);
    }
    Str substr(size_t start, size_t count = size_t(-1)) const {
        if (start >= size_)
            return {};
        if (count > size_ - start)
            count = size_ - start;
        return Str(c_str() + start, count);
    }
    bool starts(const Str& value) const {
        return value.size() <= size_ && substr(0, value.size()) == value;
    }
    bool ends(const Str& value) const {
        return value.size() <= size_ && substr(size_ - value.size()) == value;
    }
    Str replace(const Str& from, const Str& to) const {
        if (from.empty())
            return *this;
        Str out;
        size_t pos = 0;
        for (;;) {
            size_t at = find(from, pos);
            if (at == size_t(-1)) {
                out.append(substr(pos));
                return out;
            }
            out.append(substr(pos, at - pos));
            out.append(to);
            pos = at + from.size();
        }
    }
    Str trim() const {
        size_t a = 0, b = size_;
        while (a < b && ((*this)[a] == ' ' || (*this)[a] == '\t' || (*this)[a] == '\r' || (*this)[a] == '\n'))
            ++a;
        while (b > a && ((*this)[b - 1] == ' ' || (*this)[b - 1] == '\t' || (*this)[b - 1] == '\r' ||
                         (*this)[b - 1] == '\n'))
            --b;
        return substr(a, b - a);
    }
    Str lower() const {
        Str out = *this;
        for (size_t i = 0; i < out.size_; ++i)
            if (out.data_[i] >= 'A' && out.data_[i] <= 'Z')
                out.data_[i] += 32;
        return out;
    }
    bool operator==(const Str& other) const {
        if (size_ != other.size_)
            return false;
        for (size_t i = 0; i < size_; ++i)
            if ((*this)[i] != other[i])
                return false;
        return true;
    }
    bool operator!=(const Str& other) const {
        return !(*this == other);
    }
};
inline Str operator+(const Str& a, const Str& b) {
    Str out = a;
    out.append(b);
    return out;
}
inline Str operator+(const char* a, const Str& b) {
    return Str(a) + b;
}
inline Str operator+(const Str& a, const char* b) {
    return a + Str(b);
}

template <class T> class List {
    T* data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;

  public:
    List() = default;
    List(const List& other) {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i)
            add(other[i]);
    }
    List(List&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }
    ~List() {
        clear();
        ap_deallocate(data_);
    }
    List& operator=(List other) noexcept {
        swap(other);
        return *this;
    }
    void swap(List& other) noexcept {
        T* p = data_;
        data_ = other.data_;
        other.data_ = p;
        size_t n = size_;
        size_ = other.size_;
        other.size_ = n;
        n = capacity_;
        capacity_ = other.capacity_;
        other.capacity_ = n;
    }
    size_t size() const {
        return size_;
    }
    bool empty() const {
        return !size_;
    }
    T& operator[](size_t i) {
        return data_[i];
    }
    const T& operator[](size_t i) const {
        return data_[i];
    }
    T* begin() {
        return data_;
    }
    T* end() {
        return data_ ? data_ + size_ : nullptr;
    }
    const T* begin() const {
        return data_;
    }
    const T* end() const {
        return data_ ? data_ + size_ : nullptr;
    }
    void reserve(size_t capacity) {
        if (capacity <= capacity_)
            return;
        if (capacity > size_t(-1) / sizeof(T))
            ap_panic("Array overflow");
        size_t next = capacity_ ? capacity_ * 2 : 8;
        if (next < capacity || next > size_t(-1) / sizeof(T))
            next = capacity;
        T* p = static_cast<T*>(ap_allocate(next * sizeof(T)));
        if (!p)
            ap_panic("Out of memory");
        for (size_t i = 0; i < size_; ++i) {
            new (ApPlacement{}, p + i) T(move(data_[i]));
            data_[i].~T();
        }
        ap_deallocate(data_);
        data_ = p;
        capacity_ = next;
    }
    void add(const T& value) {
        T copy(value);
        reserve(size_ + 1);
        new (ApPlacement{}, data_ + size_) T(move(copy));
        ++size_;
    }
    void add(T&& value) {
        T copy(move(value));
        reserve(size_ + 1);
        new (ApPlacement{}, data_ + size_) T(move(copy));
        ++size_;
    }
    void clear() {
        for (size_t i = 0; i < size_; ++i)
            data_[i].~T();
        size_ = 0;
    }
    void erase(size_t index) {
        if (index >= size_)
            return;
        for (size_t i = index; i + 1 < size_; ++i)
            data_[i] = move(data_[i + 1]);
        data_[size_ - 1].~T();
        --size_;
    }
};

Str number(uint64_t value);
bool unsignedNumber(const Str& text, uint64_t& value);
bool utf8(const Str& text);
bool relativePath(const Str& path, Str& error);
bool absolutePath(const Str& path);
Str slash(const Str& path);
Str parent(const Str& path);
Str leaf(const Str& path);
Str join(const Str& a, const Str& b);
List<Str> split(const Str& value, char separator);
struct Status {
    bool ok = true;
    Str message;
    static Status fail(const Str& message) {
        return {false, message};
    }
    explicit operator bool() const {
        return ok;
    }
};
} // namespace ap

#define AP_TRY(expression)                                                                                   \
    do {                                                                                                     \
        ::ap::Status ap_status_ = (expression);                                                              \
        if (!ap_status_)                                                                                     \
            return ap_status_;                                                                               \
    } while (false)

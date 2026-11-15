#pragma once
#include "ap/base.hpp"
namespace ap {
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    Str text;
    bool flag = false;
    List<Str> keys;
    List<Json> values;
    Json() = default;
    explicit Json(const Str& value) : kind(String), text(value) {}
    explicit Json(const char* value) : kind(String), text(value) {}
    static Json object() {
        Json j;
        j.kind = Object;
        return j;
    }
    static Json array() {
        Json j;
        j.kind = Array;
        return j;
    }
    static Json boolean(bool value) {
        Json j;
        j.kind = Bool;
        j.flag = value;
        return j;
    }
    static Json integer(uint64_t value) {
        Json j;
        j.kind = Number;
        j.text = number(value);
        return j;
    }
    const Json& get(const Str& key) const;
    Json* find(const Str& key);
    bool has(const Str& key) const {
        return get(key).kind != Null;
    }
    Str str(const Str& key, const Str& fallback = {}) const {
        const Json& v = get(key);
        return v.kind == String ? v.text : fallback;
    }
    bool boolean(const Str& key, bool fallback) const {
        const Json& v = get(key);
        return v.kind == Bool ? v.flag : fallback;
    }
    uint64_t integer(const Str& key, uint64_t fallback) const {
        uint64_t n;
        const Json& v = get(key);
        return v.kind == Number && unsignedNumber(v.text, n) ? n : fallback;
    }
    void set(const Str& key, Json value);
    void erase(const Str& key);
    void add(Json value) {
        if (kind == Null)
            kind = Array;
        values.add(move(value));
    }
    bool same(const Json& other) const;
    Str dump() const;
};
Status parseJson(const Str& input, Json& output);
Str jsonQuote(const Str& input);
} // namespace ap

#include "ap/package.hpp"
#include <stdio.h>
using namespace ap;
namespace {
int failed = 0;
void check(bool ok, const char* label) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", label);
        ++failed;
    }
}
} // namespace
int main() {
    check(sha256("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 empty vector");
    check(sha256("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc vector");
    Sha256 hash;
    char block[1000];
    ap_memset(block, 'a', sizeof(block));
    for (unsigned i = 0; i < 1000; ++i)
        hash.update(block, sizeof(block));
    check(hash.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 streaming million-byte vector");
    Json json;
    check(bool(parseJson(
              "{\"name\":\"\\u661f\\u6cb3\\ud83d\\ude80\",\"a\":[true,false,null,18446744073709551615]}",
              json)),
          "JSON Unicode and integer parsing");
    check(json.str("name") == "星河🚀", "JSON surrogate pair decoding");
    Json roundtrip;
    check(bool(parseJson(json.dump(), roundtrip)) && roundtrip.dump() == json.dump(),
          "JSON serialization round trip");
    const char* invalid[] = {"{\"a\":1,\"a\":2}", "[1,]",  "01",        "1e",     "\"\\ud800\"",
                             "\"\\udc00\"",       "{x:1}", "null true", "\"\n\"", "["};
    for (const auto* text : invalid) {
        Json parsed;
        check(!parseJson(text, parsed), "Malformed JSON rejected");
    }
    Str deep;
    for (unsigned i = 0; i < 100; ++i)
        deep.push('[');
    deep.append("0");
    for (unsigned i = 0; i < 100; ++i)
        deep.push(']');
    check(!parseJson(deep, roundtrip), "JSON depth limit");
    uint64_t number = 0;
    check(unsignedNumber("18446744073709551615", number) && number == UINT64_MAX, "u64 maximum");
    check(!unsignedNumber("18446744073709551616", number), "u64 overflow");
    Str pathError;
    const char* unsafe[] = {"../escape", "/absolute",      "C:/absolute", "a/../../b", "a//b",
                            "a\\b",      "a/.accretion/b", "a/./b",       "x:y"};
    for (const auto* text : unsafe)
        check(!relativePath(text, pathError), "Unsafe archive path rejected");
    check(relativePath("目录/bin/application", pathError), "Safe UTF-8 archive path");
    check(!utf8(Str("\xc0\x80", 2)) && !utf8(Str("\xed\xa0\x80", 3)) && !utf8(Str("\xf4\x90\x80\x80", 4)),
          "Malformed UTF-8 rejected");
    check(os::shellQuote("a'b") == "'a'\\''b'", "POSIX literal quoting");
    check(os::commandQuote("a\\\"b") == "\"a\\\\\\\"b\"", "Windows argument quoting");
    Str self = "abcdefghijklmnopqrstuvwx";
    self.append(self);
    check(self == "abcdefghijklmnopqrstuvwxabcdefghijklmnopqrstuvwx", "Aliased string append");
    List<Str> values;
    for (int i = 0; i < 8; ++i)
        values.add("item");
    values.add(values[0]);
    check(values[8] == "item", "Aliased vector growth");
    printf("Core checks: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
}

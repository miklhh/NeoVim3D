// Minimal MessagePack value type, encoder and streaming decoder.
// Only what is needed to talk Neovim's msgpack-RPC protocol.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mp {

struct Value;
using Array = std::vector<Value>;
using Map = std::vector<std::pair<Value, Value>>;

struct Ext {
    int8_t type = 0;
    std::string data;
};

struct Value {
    using Storage = std::variant<std::monostate, bool, int64_t, double, std::string,
                                 std::shared_ptr<Array>, std::shared_ptr<Map>, Ext>;
    Storage v;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(int i) : v(int64_t(i)) {}
    Value(int64_t i) : v(i) {}
    Value(uint64_t i) : v(int64_t(i)) {}
    Value(double d) : v(d) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(std::string_view s) : v(std::string(s)) {}
    Value(Array a) : v(std::make_shared<Array>(std::move(a))) {}
    Value(Map m) : v(std::make_shared<Map>(std::move(m))) {}
    Value(Ext e) : v(std::move(e)) {}

    bool is_nil() const { return std::holds_alternative<std::monostate>(v); }
    bool is_bool() const { return std::holds_alternative<bool>(v); }
    bool is_int() const { return std::holds_alternative<int64_t>(v); }
    bool is_double() const { return std::holds_alternative<double>(v); }
    bool is_str() const { return std::holds_alternative<std::string>(v); }
    bool is_array() const { return std::holds_alternative<std::shared_ptr<Array>>(v); }
    bool is_map() const { return std::holds_alternative<std::shared_ptr<Map>>(v); }
    bool is_ext() const { return std::holds_alternative<Ext>(v); }

    // Lenient accessors: return a default if the type does not match.
    bool as_bool(bool def = false) const;
    int64_t as_int(int64_t def = 0) const;
    double as_double(double def = 0.0) const;
    const std::string& as_str() const;
    const Array& as_array() const;
    const Map& as_map() const;

    // Map lookup by string key; returns nil value if missing.
    const Value& operator[](std::string_view key) const;
    // Array index; returns nil value if out of range.
    const Value& operator[](size_t idx) const;
    size_t size() const;
};

const Value& nil();

void encode(std::string& out, const Value& v);
std::string encode(const Value& v);

// Pull-based decoder. `read` must fill up to n bytes into buf and return the
// count read, or 0 on EOF.
class Decoder {
public:
    using ReadFn = std::function<size_t(char* buf, size_t n)>;
    explicit Decoder(ReadFn read) : read_(std::move(read)) {}

    // Decode the next value. Throws EndOfStream on EOF.
    Value next();

    struct EndOfStream : std::runtime_error {
        EndOfStream() : std::runtime_error("end of msgpack stream") {}
    };

private:
    uint8_t byte();
    void bytes(char* dst, size_t n);
    uint64_t be(int n);
    Value array(size_t n);
    Value map(size_t n);
    Value str(size_t n);
    Value ext(size_t n);

    ReadFn read_;
    std::vector<char> buf_ = std::vector<char>(1 << 16);
    size_t pos_ = 0, len_ = 0;
};

// Decode a complete in-memory buffer (used in tests).
Value decode(std::string_view data);

}  // namespace mp

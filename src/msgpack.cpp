#include "msgpack.hpp"

#include <cstring>

namespace mp {

namespace {
const Value kNil;
const std::string kEmptyStr;
const Array kEmptyArray;
const Map kEmptyMap;

void put_be(std::string& out, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i) out.push_back(char((v >> (i * 8)) & 0xff));
}
}  // namespace

const Value& nil() { return kNil; }

bool Value::as_bool(bool def) const {
    if (auto* b = std::get_if<bool>(&v)) return *b;
    if (auto* i = std::get_if<int64_t>(&v)) return *i != 0;
    return def;
}

int64_t Value::as_int(int64_t def) const {
    if (auto* i = std::get_if<int64_t>(&v)) return *i;
    if (auto* d = std::get_if<double>(&v)) return int64_t(*d);
    if (auto* b = std::get_if<bool>(&v)) return *b;
    return def;
}

double Value::as_double(double def) const {
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* i = std::get_if<int64_t>(&v)) return double(*i);
    return def;
}

const std::string& Value::as_str() const {
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    return kEmptyStr;
}

const Array& Value::as_array() const {
    if (auto* a = std::get_if<std::shared_ptr<Array>>(&v)) return **a;
    return kEmptyArray;
}

const Map& Value::as_map() const {
    if (auto* m = std::get_if<std::shared_ptr<Map>>(&v)) return **m;
    return kEmptyMap;
}

const Value& Value::operator[](std::string_view key) const {
    for (auto& [k, val] : as_map())
        if (k.is_str() && k.as_str() == key) return val;
    return kNil;
}

const Value& Value::operator[](size_t idx) const {
    auto& a = as_array();
    return idx < a.size() ? a[idx] : kNil;
}

size_t Value::size() const {
    if (is_array()) return as_array().size();
    if (is_map()) return as_map().size();
    return 0;
}

void encode(std::string& out, const Value& val) {
    std::visit(
        [&](auto&& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                out.push_back(char(0xc0));
            } else if constexpr (std::is_same_v<T, bool>) {
                out.push_back(char(x ? 0xc3 : 0xc2));
            } else if constexpr (std::is_same_v<T, int64_t>) {
                if (x >= 0 && x <= 0x7f) {
                    out.push_back(char(x));
                } else if (x < 0 && x >= -32) {
                    out.push_back(char(int8_t(x)));
                } else if (x >= 0) {
                    out.push_back(char(0xcf));
                    put_be(out, uint64_t(x), 8);
                } else {
                    out.push_back(char(0xd3));
                    put_be(out, uint64_t(x), 8);
                }
            } else if constexpr (std::is_same_v<T, double>) {
                uint64_t bits;
                std::memcpy(&bits, &x, 8);
                out.push_back(char(0xcb));
                put_be(out, bits, 8);
            } else if constexpr (std::is_same_v<T, std::string>) {
                size_t n = x.size();
                if (n < 32) {
                    out.push_back(char(0xa0 | n));
                } else if (n < 0x100) {
                    out.push_back(char(0xd9));
                    put_be(out, n, 1);
                } else if (n < 0x10000) {
                    out.push_back(char(0xda));
                    put_be(out, n, 2);
                } else {
                    out.push_back(char(0xdb));
                    put_be(out, n, 4);
                }
                out += x;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
                size_t n = x->size();
                if (n < 16) {
                    out.push_back(char(0x90 | n));
                } else if (n < 0x10000) {
                    out.push_back(char(0xdc));
                    put_be(out, n, 2);
                } else {
                    out.push_back(char(0xdd));
                    put_be(out, n, 4);
                }
                for (auto& e : *x) encode(out, e);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Map>>) {
                size_t n = x->size();
                if (n < 16) {
                    out.push_back(char(0x80 | n));
                } else if (n < 0x10000) {
                    out.push_back(char(0xde));
                    put_be(out, n, 2);
                } else {
                    out.push_back(char(0xdf));
                    put_be(out, n, 4);
                }
                for (auto& [k, e] : *x) {
                    encode(out, k);
                    encode(out, e);
                }
            } else if constexpr (std::is_same_v<T, Ext>) {
                out.push_back(char(0xc7));
                put_be(out, x.data.size(), 1);
                out.push_back(char(x.type));
                out += x.data;
            }
        },
        val.v);
}

std::string encode(const Value& v) {
    std::string s;
    encode(s, v);
    return s;
}

uint8_t Decoder::byte() {
    if (pos_ == len_) {
        len_ = read_(buf_.data(), buf_.size());
        pos_ = 0;
        if (len_ == 0) throw EndOfStream();
    }
    return uint8_t(buf_[pos_++]);
}

void Decoder::bytes(char* dst, size_t n) {
    while (n > 0) {
        if (pos_ == len_) {
            len_ = read_(buf_.data(), buf_.size());
            pos_ = 0;
            if (len_ == 0) throw EndOfStream();
        }
        size_t k = std::min(n, len_ - pos_);
        std::memcpy(dst, buf_.data() + pos_, k);
        pos_ += k;
        dst += k;
        n -= k;
    }
}

uint64_t Decoder::be(int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | byte();
    return v;
}

Value Decoder::array(size_t n) {
    Array a;
    a.reserve(n);
    for (size_t i = 0; i < n; ++i) a.push_back(next());
    return Value(std::move(a));
}

Value Decoder::map(size_t n) {
    Map m;
    m.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Value k = next();
        Value v = next();
        m.emplace_back(std::move(k), std::move(v));
    }
    return Value(std::move(m));
}

Value Decoder::str(size_t n) {
    std::string s(n, '\0');
    bytes(s.data(), n);
    return Value(std::move(s));
}

Value Decoder::ext(size_t n) {
    Ext e;
    e.type = int8_t(byte());
    e.data.resize(n);
    bytes(e.data.data(), n);
    return Value(std::move(e));
}

Value Decoder::next() {
    uint8_t b = byte();
    if (b <= 0x7f) return Value(int64_t(b));
    if (b >= 0xe0) return Value(int64_t(int8_t(b)));
    if ((b & 0xf0) == 0x80) return map(b & 0x0f);
    if ((b & 0xf0) == 0x90) return array(b & 0x0f);
    if ((b & 0xe0) == 0xa0) return str(b & 0x1f);
    switch (b) {
        case 0xc0: return Value();
        case 0xc2: return Value(false);
        case 0xc3: return Value(true);
        case 0xc4: return str(be(1));
        case 0xc5: return str(be(2));
        case 0xc6: return str(be(4));
        case 0xc7: return ext(be(1));
        case 0xc8: return ext(be(2));
        case 0xc9: return ext(be(4));
        case 0xca: {
            uint32_t bits = uint32_t(be(4));
            float f;
            std::memcpy(&f, &bits, 4);
            return Value(double(f));
        }
        case 0xcb: {
            uint64_t bits = be(8);
            double d;
            std::memcpy(&d, &bits, 8);
            return Value(d);
        }
        case 0xcc: return Value(int64_t(be(1)));
        case 0xcd: return Value(int64_t(be(2)));
        case 0xce: return Value(int64_t(be(4)));
        case 0xcf: return Value(int64_t(be(8)));
        case 0xd0: return Value(int64_t(int8_t(be(1))));
        case 0xd1: return Value(int64_t(int16_t(be(2))));
        case 0xd2: return Value(int64_t(int32_t(be(4))));
        case 0xd3: return Value(int64_t(be(8)));
        case 0xd4: return ext(1);
        case 0xd5: return ext(2);
        case 0xd6: return ext(4);
        case 0xd7: return ext(8);
        case 0xd8: return ext(16);
        case 0xd9: return str(be(1));
        case 0xda: return str(be(2));
        case 0xdb: return str(be(4));
        case 0xdc: return array(be(2));
        case 0xdd: return array(be(4));
        case 0xde: return map(be(2));
        case 0xdf: return map(be(4));
        default: throw std::runtime_error("invalid msgpack byte");
    }
}

Value decode(std::string_view data) {
    size_t off = 0;
    Decoder d([&](char* buf, size_t n) {
        size_t k = std::min(n, data.size() - off);
        std::memcpy(buf, data.data() + off, k);
        off += k;
        return k;
    });
    return d.next();
}

}  // namespace mp

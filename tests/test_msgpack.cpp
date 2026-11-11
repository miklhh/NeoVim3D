#include "check.hpp"
#include "msgpack.hpp"

TEST(msgpack_scalars_roundtrip) {
    for (int64_t i : {int64_t(0), int64_t(1), int64_t(127), int64_t(128), int64_t(-1),
                      int64_t(-32), int64_t(-33), int64_t(70000), int64_t(-70000),
                      int64_t(1) << 40}) {
        CHECK_EQ(mp::decode(mp::encode(mp::Value(i))).as_int(), i);
    }
    CHECK(mp::decode(mp::encode(mp::Value())).is_nil());
    CHECK_EQ(mp::decode(mp::encode(mp::Value(true))).as_bool(), true);
    CHECK_EQ(mp::decode(mp::encode(mp::Value(false))).as_bool(true), false);
    CHECK_EQ(mp::decode(mp::encode(mp::Value(2.5))).as_double(), 2.5);
}

TEST(msgpack_strings) {
    for (size_t n : {size_t(0), size_t(5), size_t(31), size_t(32), size_t(255), size_t(256),
                     size_t(70000)}) {
        std::string s(n, 'x');
        CHECK_EQ(mp::decode(mp::encode(mp::Value(s))).as_str(), s);
    }
}

TEST(msgpack_containers) {
    mp::Array big;
    for (int i = 0; i < 40; ++i) big.push_back(i);
    mp::Value v = mp::Array{1, "two", mp::Map{{"k", 3}, {"list", big}}};
    mp::Value d = mp::decode(mp::encode(v));
    CHECK_EQ(d.size(), size_t(3));
    CHECK_EQ(d[1].as_str(), std::string("two"));
    CHECK_EQ(d[2]["k"].as_int(), 3);
    CHECK_EQ(d[2]["list"].size(), size_t(40));
    CHECK_EQ(d[2]["list"][39].as_int(), 39);
    CHECK(d[2]["missing"].is_nil());
    CHECK(d[7].is_nil());
}

TEST(msgpack_ext_and_float32) {
    // fixext 1 (type 0, as Nvim uses for Buffer handles) and float32 1.5.
    std::string ext("\xd4\x00\x05", 3);
    mp::Value e = mp::decode(ext);
    CHECK(e.is_ext());
    std::string f32("\xca\x3f\xc0\x00\x00", 5);
    CHECK_EQ(mp::decode(f32).as_double(), 1.5);
}

TEST(msgpack_stream_of_values) {
    std::string data = mp::encode(mp::Value(1)) + mp::encode(mp::Value("abc")) +
                       mp::encode(mp::Value(mp::Array{1, 2}));
    size_t off = 0;
    mp::Decoder dec([&](char* buf, size_t n) {
        // Feed one byte at a time to exercise buffer refills.
        if (off >= data.size() || n == 0) return size_t(0);
        buf[0] = data[off++];
        return size_t(1);
    });
    CHECK_EQ(dec.next().as_int(), 1);
    CHECK_EQ(dec.next().as_str(), std::string("abc"));
    CHECK_EQ(dec.next().size(), size_t(2));
    bool eof = false;
    try {
        dec.next();
    } catch (const mp::Decoder::EndOfStream&) {
        eof = true;
    }
    CHECK(eof);
}

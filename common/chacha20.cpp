#include "chacha20.h"
#include <cstring>

namespace {

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t load32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

#define QR(a, b, c, d)                     \
    a += b; d ^= a; d = rotl32(d, 16);     \
    c += d; b ^= c; b = rotl32(b, 12);     \
    a += b; d ^= a; d = rotl32(d, 8);      \
    c += d; b ^= c; b = rotl32(b, 7);

void block(const uint32_t in[16], uint8_t out[64])
{
    uint32_t x[16];
    std::memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; ++i) {
        QR(x[0], x[4],  x[8],  x[12])
        QR(x[1], x[5],  x[9],  x[13])
        QR(x[2], x[6],  x[10], x[14])
        QR(x[3], x[7],  x[11], x[15])
        QR(x[0], x[5],  x[10], x[15])
        QR(x[1], x[6],  x[11], x[12])
        QR(x[2], x[7],  x[8],  x[13])
        QR(x[3], x[4],  x[9],  x[14])
    }
    for (int i = 0; i < 16; ++i) {
        uint32_t v = x[i] + in[i];
        out[4 * i + 0] = uint8_t(v);
        out[4 * i + 1] = uint8_t(v >> 8);
        out[4 * i + 2] = uint8_t(v >> 16);
        out[4 * i + 3] = uint8_t(v >> 24);
    }
}

} // namespace

void ChaCha20::xorBuffer(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, uint8_t *data, size_t len)
{
    uint32_t st[16];
    st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574;
    for (int i = 0; i < 8; ++i) st[4 + i] = load32(key + 4 * i);
    st[12] = counter;
    for (int i = 0; i < 3; ++i) st[13 + i] = load32(nonce + 4 * i);

    uint8_t ks[64];
    size_t off = 0;
    while (off < len) {
        block(st, ks);
        const size_t n = (len - off < 64) ? (len - off) : 64;
        for (size_t i = 0; i < n; ++i) data[off + i] ^= ks[i];
        off += n;
        ++st[12];
    }
}

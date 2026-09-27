// ChaCha20 (RFC 8439) - implementation autonome, aucune dependance.
#pragma once
#include <cstdint>
#include <cstddef>

namespace ChaCha20 {
// key : 32 octets, nonce : 12 octets. Chiffre/dechiffre sur place.
void xorBuffer(const uint8_t key[32], const uint8_t nonce[12],
               uint32_t counter, uint8_t *data, size_t len);
}

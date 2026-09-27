// Derivation de cle et authentification defi/reponse.
#pragma once
#include <QByteArray>
#include <QString>

namespace rr {

// PBKDF2-HMAC-SHA256, 60 000 iterations -> 32 octets.
QByteArray deriveKey(const QString &password, const QByteArray &salt);

// HMAC-SHA256 tronque a 32 octets.
QByteArray hmac(const QByteArray &key, const QByteArray &data);

// Octets aleatoires cryptographiques.
QByteArray randomBytes(int n);

// Sous-cle deterministe pour un usage donne ("ptt", "udp", ...).
QByteArray subKey(const QByteArray &master, const char *label);

} // namespace rr

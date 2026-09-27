#include "crypto.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>
#include <QRandomGenerator>

namespace rr {

QByteArray deriveKey(const QString &password, const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(), salt, 60000, 32);
}

QByteArray hmac(const QByteArray &key, const QByteArray &data)
{
    QMessageAuthenticationCode m(QCryptographicHash::Sha256, key);
    m.addData(data);
    return m.result();
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, 0);
    QRandomGenerator::system()->generate(b.begin(), b.end());
    return b;
}

QByteArray subKey(const QByteArray &master, const char *label)
{
    return hmac(master, QByteArray("RemoteRig/") + label);
}

} // namespace rr

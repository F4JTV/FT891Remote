// A rigctld-compatible port on the client, 127.0.0.1:4532 by default.
//
// WSJT-X, fldigi or JS8Call connect to it as "Hamlib NET rigctl" and drive
// the remote FT-891: frequency, mode, split and PTT. Nothing of Hamlib is
// linked; this only speaks its network protocol.
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QVariantMap>

#include "../common/protocol.h"

namespace rr {

class RigctldServer : public QObject {
    Q_OBJECT
public:
    explicit RigctldServer(QObject *parent = nullptr);

    bool start(quint16 port = 4532, bool localOnly = true);
    void stop();
    bool running() const;
    quint16 port() const { return m_port; }

public slots:
    void updateState(const rr::Ft891State &st);
    void updateValues(const QVariantMap &values);

signals:
    void requestCommand(const QString &name, const QVariantMap &args);
    void requestPtt(bool on);
    void logMessage(const QString &msg);

private:
    QByteArray handleLine(const QString &line);
    bool split() const;
    static QString dumpState();

    QTcpServer  m_server;
    Ft891State  m_state;
    QVariantMap m_values;
    quint16     m_port = 4532;
};

} // namespace rr

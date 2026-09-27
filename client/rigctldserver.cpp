#include "rigctldserver.h"

#include "../common/ft891.h"

#include <QRegularExpression>
#include <QTcpSocket>

namespace rr {

RigctldServer::RigctldServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        QTcpSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        auto *buf = new QByteArray;

        connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
            buf->append(s->readAll());
            int nl;
            while ((nl = buf->indexOf('\n')) >= 0) {
                const QString line = QString::fromLatin1(buf->left(nl)).trimmed();
                buf->remove(0, nl + 1);
                if (line == QLatin1String("q") || line == QLatin1String("Q")) {
                    s->disconnectFromHost();
                    return;
                }
                const QByteArray reply = handleLine(line);
                if (!reply.isEmpty()) s->write(reply);
            }
        });
        connect(s, &QTcpSocket::disconnected, this, [s, buf] {
            delete buf;
            s->deleteLater();
        });
        emit logMessage(tr("Local application connected to the rigctld port"));
    });
}

bool RigctldServer::start(quint16 port, bool localOnly)
{
    stop();
    m_port = port;
    const bool ok = m_server.listen(localOnly ? QHostAddress::LocalHost : QHostAddress::Any, port);
    emit logMessage(ok ? tr("rigctld interface listening on %1:%2")
                             .arg(localOnly ? QStringLiteral("127.0.0.1") : QStringLiteral("0.0.0.0"))
                             .arg(port)
                       : tr("rigctld port %1 unavailable").arg(port));
    return ok;
}

void RigctldServer::stop() { if (m_server.isListening()) m_server.close(); }
bool RigctldServer::running() const { return m_server.isListening(); }

void RigctldServer::updateState(const Ft891State &st) { m_state = st; }

void RigctldServer::updateValues(const QVariantMap &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) m_values.insert(it.key(), it.value());
}

bool RigctldServer::split() const
{
    const QString v = m_values.value(QStringLiteral("SPL")).toString();
    return !v.isEmpty() && !v.startsWith(QLatin1Char('0'));
}

QByteArray RigctldServer::handleLine(const QString &raw)
{
    // Hamlib may prefix the command with a separator in extended mode; the
    // short answer format is accepted by WSJT-X and fldigi alike.
    QString line = raw;
    if (!line.isEmpty() && QStringLiteral("+;|,").contains(line.at(0))) line = line.mid(1);
    line = line.trimmed();
    if (line.isEmpty()) return {};

    QStringList a = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QString cmd = a.value(0);
    const ft891::ModeInfo *mode = ft891::modeByName(m_state.mode);

    // A client in VFO mode — fldigi, or any Hamlib client with vfo_opt on —
    // names the VFO before the values: "F VFOA 7030000", "T VFOA 1". The
    // name is taken out here. Read as the value, it turned every frequency
    // into 0 and every PTT into "off", all answered RPRT 0: nothing moved.
    static const QStringList vfoNames = {
        QStringLiteral("VFOA"), QStringLiteral("VFOB"), QStringLiteral("VFOC"), QStringLiteral("currVFO"),
        QStringLiteral("VFO"), QStringLiteral("Main"), QStringLiteral("MainA"), QStringLiteral("MainB"),
        QStringLiteral("Sub"), QStringLiteral("SubA"), QStringLiteral("SubB"), QStringLiteral("MEM"),
        QStringLiteral("TX"), QStringLiteral("RX"), QStringLiteral("None")};
    // The name itself is not acted on: the FT-891 receives on VFO-A, and
    // VFO-B is only the transmit frequency of split, reached through the
    // split commands (I, i). fldigi's Hamlib leaves its current VFO on B
    // after probing both at start-up, then polls and sets "VFOB": taken at
    // its word, the display followed VFO-B and every tuning went there.
    const bool setVfoCmd = cmd == QLatin1String("V") || cmd == QLatin1String("set_vfo");
    if (!setVfoCmd && a.size() > 1 && vfoNames.contains(a.at(1))) a.removeAt(1);

    if (cmd == QLatin1String("\\dump_state"))  return dumpState().toLatin1();
    if (cmd == QLatin1String("\\chk_vfo"))     return "CHKVFO 0\n";
    if (cmd == QLatin1String("\\set_vfo_opt")) return "RPRT 0\n";   // names are accepted either way
    if (cmd == QLatin1String("\\get_powerstat") || cmd == QLatin1String("get_powerstat"))
        return m_state.radioOn ? "1\n" : "0\n";

    if (cmd == QLatin1String("f") || cmd == QLatin1String("get_freq"))
        return QByteArray::number(qulonglong(m_state.freqA)) + "\n";

    if (cmd == QLatin1String("F") || cmd == QLatin1String("set_freq")) {
        const double hz = a.value(1).toDouble();
        if (hz <= 0) return "RPRT -1\n";   // RIG_EINVAL: not a frequency
        emit requestCommand(QStringLiteral("freq"),
                            {{QStringLiteral("hz"), hz}, {QStringLiteral("vfo"), QStringLiteral("A")}});
        return "RPRT 0\n";
    }

    if (cmd == QLatin1String("m") || cmd == QLatin1String("get_mode")) {
        const QString name = mode ? mode->hamlib : QStringLiteral("USB");
        const int pb = mode ? mode->width : 2400;
        return (name + QLatin1Char('\n') + QString::number(pb) + QLatin1Char('\n')).toLatin1();
    }

    if (cmd == QLatin1String("M") || cmd == QLatin1String("set_mode")) {
        const ft891::ModeInfo *m = ft891::modeByName(a.value(1));
        if (!m) return "RPRT -1\n";   // RIG_EINVAL
        // The passband is the radio's own, per mode: it is not changed here.
        emit requestCommand(QStringLiteral("mode"), {{QStringLiteral("v"), m->name}});
        return "RPRT 0\n";
    }

    if (cmd == QLatin1String("t") || cmd == QLatin1String("get_ptt"))
        return m_state.ptt ? "1\n" : "0\n";

    if (cmd == QLatin1String("T") || cmd == QLatin1String("set_ptt")) {
        // 0 receive; 1, 2 and 3 transmit (plain, microphone, data: fldigi
        // sends 3).
        bool ok = false;
        const int v = a.value(1).toInt(&ok);
        if (!ok || v < 0 || v > 3) return "RPRT -1\n";
        emit requestPtt(v != 0);
        return "RPRT 0\n";
    }

    if (cmd == QLatin1String("v") || cmd == QLatin1String("get_vfo"))
        return "VFOA\n";
    // The FT-891 receives on VFO-A: VFO-B is only a transmit frequency in
    // split. Selecting a VFO is accepted and ignored.
    if (cmd == QLatin1String("V") || cmd == QLatin1String("set_vfo"))
        return "RPRT 0\n";

    if (cmd == QLatin1String("s") || cmd == QLatin1String("get_split_vfo"))
        return split() ? "1\nVFOB\n" : "0\nVFOA\n";

    if (cmd == QLatin1String("S") || cmd == QLatin1String("set_split_vfo")) {
        const bool on = a.value(1).toInt() != 0;
        emit requestCommand(QStringLiteral("set"),
                            {{QStringLiteral("code"), QStringLiteral("SPL")},
                             {QStringLiteral("v"), QStringList{on ? QStringLiteral("1") : QStringLiteral("0")}}});
        return "RPRT 0\n";
    }

    if (cmd == QLatin1String("i") || cmd == QLatin1String("get_split_freq"))
        return QByteArray::number(qulonglong(m_state.freqB)) + "\n";

    if (cmd == QLatin1String("I") || cmd == QLatin1String("set_split_freq")) {
        const double hz = a.value(1).toDouble();
        if (hz <= 0) return "RPRT -1\n";
        emit requestCommand(QStringLiteral("freq"),
                            {{QStringLiteral("hz"), hz}, {QStringLiteral("vfo"), QStringLiteral("B")}});
        return "RPRT 0\n";
    }

    // One mode for both VFOs: the split mode is the current mode.
    if (cmd == QLatin1String("x") || cmd == QLatin1String("get_split_mode")) {
        const QString name = mode ? mode->hamlib : QStringLiteral("USB");
        return (name + QStringLiteral("\n") + QString::number(mode ? mode->width : 2400) + QStringLiteral("\n")).toLatin1();
    }
    if (cmd == QLatin1String("X") || cmd == QLatin1String("set_split_mode"))
        return "RPRT 0\n";

    if (cmd == QLatin1String("l") || cmd == QLatin1String("get_level")) {
        if (a.value(1) == QLatin1String("STRENGTH"))
            return QByteArray::number(qRound(ft891::sMeterDb(m_state.sMeter))) + "\n";
        if (a.value(1) == QLatin1String("SWR"))
            return QByteArray::number(ft891::swrValue(m_state.swr), 'f', 2) + "\n";
        if (a.value(1) == QLatin1String("RFPOWER")) {
            const double w = m_values.value(QStringLiteral("PWR")).toDouble();
            return QByteArray::number(w / 100.0, 'f', 2) + "\n";
        }
        return "0\n";
    }

    if (cmd == QLatin1String("L") || cmd == QLatin1String("set_level")
        || cmd == QLatin1String("U") || cmd == QLatin1String("set_func"))
        return "RPRT 0\n";

    return "RPRT -11\n";   // RIG_ENAVAIL
}

QString RigctldServer::dumpState()
{
    // The FT-891's ranges: 30 kHz to 56 MHz receive, HF and 6 m transmit.
    const QString modes = QStringLiteral("0x1ff");
    QString s;
    s += QStringLiteral("0\n");      // protocol version
    s += QStringLiteral("2\n");      // model (NET rigctl)
    s += QStringLiteral("1\n");      // ITU region
    s += QStringLiteral("30000.000000 56000000.000000 ") + modes + QStringLiteral(" -1 -1 0x3 0x3\n");
    s += QStringLiteral("0 0 0 0 0 0 0\n");
    s += QStringLiteral("1800000.000000 54000000.000000 ") + modes + QStringLiteral(" 5000 100000 0x3 0x3\n");
    s += QStringLiteral("0 0 0 0 0 0 0\n");
    s += modes + QStringLiteral(" 10\n");
    s += modes + QStringLiteral(" 100\n");
    s += QStringLiteral("0 0\n");
    s += QStringLiteral("0xc 2400\n");     // SSB
    s += QStringLiteral("0x82 500\n");     // CW
    s += QStringLiteral("0x1 6000\n");     // AM
    s += QStringLiteral("0x20 12000\n");   // FM
    s += QStringLiteral("0 0\n");
    s += QStringLiteral("9999\n");         // max_rit
    s += QStringLiteral("9999\n");         // max_xit
    s += QStringLiteral("1200\n");         // max_ifshift
    s += QStringLiteral("0\n");            // announces
    s += QStringLiteral("0\n");            // preamps
    s += QStringLiteral("0\n");            // attenuators
    s += QStringLiteral("0x0\n");          // has_get_func
    s += QStringLiteral("0x0\n");          // has_set_func
    s += QStringLiteral("0x40000000\n");   // has_get_level: STRENGTH
    s += QStringLiteral("0x0\n");          // has_set_level
    s += QStringLiteral("0x0\n");          // has_get_parm
    s += QStringLiteral("0x0\n");          // has_set_parm
    return s;
}

} // namespace rr

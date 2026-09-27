// Serial link to the FT-891: the only place that touches the CAT port.
//
// Every FT-891 answer is a self-describing frame ending in « ; » — « FA… »,
// « SM0… », « EX0101… ». So answers are dispatched by what they say, not by
// the order in which requests went out. A late answer can never be credited
// to the wrong command, and a command that expects no answer (a setting, the
// PTT) can be written at any time, even while a read is pending.
//
// Requests wait in five queues. The operator comes first; read-backs of what
// he just changed next; the regular poll after that; bulk reads of the menus
// last, in the time the poll leaves free. Queued requests with the same key
// replace each other: a knob turned quickly sends its last position, not every
// position it went through.
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QSerialPort;

namespace rr {

class Ft891Link : public QObject {
    Q_OBJECT
public:
    enum Priority { User = 0, ReadBack, Poll, Bulk, PriorityCount };

    explicit Ft891Link(QObject *parent = nullptr);
    ~Ft891Link() override;

    bool open(const QString &portName, int baud, QString *error);
    void close();
    bool isOpen() const;
    QString portName() const { return m_portName; }

    // A request that expects an answer: frame « FA; », expect « FA ».
    // Without expect, the frame is a setting and gets no answer.
    // An empty key disables coalescing.
    // timeoutMs: this request's own wait for an answer, 0 for the usual one.
    void submit(const QByteArray &frame, const QByteArray &expect,
                Priority prio, const QString &key = QString(), int timeoutMs = 0);

    // Written at once, ahead of every queue. For the PTT, and for anything
    // whose delay the operator would hear or feel.
    void writeNow(const QByteArray &frame);

    // Raw bytes, for the power-on wake-up of a sleeping radio.
    void writeRaw(const QByteArray &bytes);

    int  queued(Priority prio) const { return m_queues[prio].size(); }
    int  queuedTotal() const;
    bool isQueued(const QString &key) const;
    void dropQueue(Priority prio);

    // Modem lines of the CAT port itself, for PTT or keying on the same port.
    void setRts(bool on);
    void setDtr(bool on);

    void setReadTimeout(int ms) { m_readTimeoutMs = ms; }
    void setWriteGap(int ms)    { m_gapMs = ms; }

signals:
    // An answer, without its terminator.
    void frameReceived(const QByteArray &frame);
    // A request the radio refused (« ?; ») or left unanswered.
    void requestFailed(const QByteArray &frame, bool refused);
    // Every frame written, for the traffic log.
    void frameSent(const QByteArray &frame);
    // The port failed: USB cable pulled, adapter reset.
    void linkError(const QString &message);

private slots:
    void onReadyRead();
    void onTimeout();
    void pump();

private:
    struct Request {
        QByteArray frame;
        QByteArray expect;
        QString    key;
        int        timeoutMs = 0;
    };

    void startNext();
    void completeInflight();
    void writeFrame(const QByteArray &frame);
    void handleFrame(const QByteArray &frame);

    QSerialPort    *m_port = nullptr;
    QString         m_portName;
    QList<Request>  m_queues[PriorityCount];
    bool            m_hasInflight = false;
    Request         m_inflight;
    QTimer          m_timeout;
    QTimer          m_gap;
    QByteArray      m_rx;
    QByteArray      m_lastSet;           // to credit a late « ?; »
    QElapsedTimer   m_lastSetClock;
    int             m_readTimeoutMs = 400;
    int             m_gapMs = 20;
};

} // namespace rr

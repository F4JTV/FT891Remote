#include "cwkeyer.h"

#include <QSerialPort>
#include <QTimer>

namespace rr {

namespace {

// Alphabet morse international. Le point est '.', le trait '-'.
struct MorseEntry { char c; const char *code; };
const MorseEntry kMorse[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"},{',', "--..--"},{'?', "..--.."},{'\'', ".----."},
    {'!', "-.-.--"},{'/', "-..-."}, {'(', "-.--."}, {')', "-.--.-"},
    {'&', ".-..."}, {':', "---..."},{';', "-.-.-."},{'=', "-...-"},
    {'+', ".-.-."}, {'-', "-....-"},{'_', "..--.-"},{'"', ".-..-."},
    {'$', "...-..-"},{'@', ".--.-."},
};

const char *codeFor(QChar ch)
{
    const char c = ch.toUpper().toLatin1();
    for (const MorseEntry &e : kMorse)
        if (e.c == c) return e.code;
    return nullptr;
}

} // namespace

CwKeyer::CwKeyer(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    // Minuteur precis : a 40 mots par minute, un point dure 30 ms, et la
    // granularite grossiere par defaut suffirait a rendre le texte illisible.
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &CwKeyer::step);
}

CwKeyer::~CwKeyer() { close(); }

void CwKeyer::open(const rr::CwKeyerConfig &cfg)
{
    close();
    m_cfg = cfg;
    if (!cfg.enabled || cfg.port.isEmpty()) return;

    m_port = new QSerialPort(this);
    m_port->setPortName(cfg.port);
    if (!m_port->open(QIODevice::ReadWrite)) {
        emit logMessage(tr("CW keyer: cannot open %1 — %2")
                            .arg(cfg.port, m_port->errorString()));
        delete m_port;
        m_port = nullptr;
        return;
    }
    keyLine(false);
    emit logMessage(tr("CW keyer on %1, %2 line, %3 WPM")
                        .arg(cfg.port, cfg.line).arg(cfg.wpm));
}

void CwKeyer::close()
{
    stop();
    if (m_port) {
        keyLine(false);
        m_port->close();
        delete m_port;
        m_port = nullptr;
    }
}

void CwKeyer::setWpm(int wpm)
{
    m_cfg.wpm = qBound(5, wpm, 60);
}

// Etat bas ou haut de la ligne de manipulation. L'inversion sert aux
// interfaces qui presentent la ligne a l'envers.
void CwKeyer::keyLine(bool down)
{
    if (!m_port) return;
    const bool level = m_cfg.inverted ? !down : down;
    if (m_cfg.line.compare(QLatin1String("RTS"), Qt::CaseInsensitive) == 0)
        m_port->setRequestToSend(level);
    else
        m_port->setDataTerminalReady(level);
}

// Construit la suite d'elements du texte.
//
// Un point dure 1200/WPM millisecondes. Entre deux elements d'un meme
// caractere, un silence d'un point ; entre deux caracteres, trois ; entre deux
// mots, sept. Ce sont les proportions de la norme, celles auxquelles une
// oreille exercee s'attend.
void CwKeyer::queueText(const QString &text)
{
    const int dot = qMax(1, 1200 / qBound(5, m_cfg.wpm, 60));
    // La correction raccourcit chaque element sans toucher aux silences : elle
    // compense le temps que met le poste a etablir sa porteuse.
    const int keyed = qMax(1, dot - m_cfg.correctionMs);

    bool firstChar = true;
    const QStringList words = text.simplified().toUpper().split(QLatin1Char(' '),
                                                                Qt::SkipEmptyParts);
    for (int w = 0; w < words.size(); ++w) {
        if (w > 0) m_elements.enqueue({false, 7 * dot});
        const QString &word = words.at(w);
        for (int i = 0; i < word.size(); ++i) {
            const char *code = codeFor(word.at(i));
            if (!code) continue;
            if (!firstChar && i > 0) m_elements.enqueue({false, 3 * dot});
            firstChar = false;
            for (const char *p = code; *p; ++p) {
                if (p != code) m_elements.enqueue({false, dot});
                m_elements.enqueue({true, (*p == '-' ? 3 * keyed : keyed)});
            }
        }
        firstChar = false;
    }
}

void CwKeyer::send(const QString &text)
{
    if (!m_port) {
        emit logMessage(tr("CW keyer: no port open"));
        return;
    }
    const bool wasIdle = m_elements.isEmpty();
    if (!wasIdle) m_elements.enqueue({false, 7 * qMax(1, 1200 / qBound(5, m_cfg.wpm, 60))});
    queueText(text);
    if (m_elements.isEmpty()) return;

    if (wasIdle) {
        m_sending = true;
        emit sendingChanged(true);
        if (m_cfg.holdPtt) emit pttRequested(true);
        // Les echeances sont comptees depuis un instant de reference, jamais
        // les unes par rapport aux autres : le retard d'un minuteur ne se
        // reporte pas sur les elements suivants, et le rythme ne derive pas.
        m_clock.start();
        m_deadline = 0;
        step();
    }
}

void CwKeyer::stop()
{
    const bool was = m_sending;
    m_elements.clear();
    m_timer->stop();
    keyLine(false);
    m_sending = false;
    if (was) {
        if (m_cfg.holdPtt) emit pttRequested(false);
        emit sendingChanged(false);
    }
}

void CwKeyer::step()
{
    if (m_elements.isEmpty()) {
        keyLine(false);
        if (m_sending) {
            m_sending = false;
            if (m_cfg.holdPtt) emit pttRequested(false);
            emit sendingChanged(false);
        }
        return;
    }

    const Element e = m_elements.dequeue();
    keyLine(e.down);

    m_deadline += e.ms;
    const qint64 wait = m_deadline - m_clock.elapsed();
    m_timer->start(int(qMax<qint64>(0, wait)));
}

} // namespace rr

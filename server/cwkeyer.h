// Manipulateur telegraphique genere par le serveur.
//
// Certains postes n'acceptent pas de texte libre par le CAT : leur commande de
// manipulateur ne sait que rejouer leurs propres memoires. C'est le cas des
// Yaesu HF. On genere donc les elements ici, a cote du poste, et l'on bascule
// une ligne serie cablee sur sa prise KEY. Le reseau n'intervient jamais dans
// l'espacement : seul le texte voyage, la cadence est produite sur place.
#pragma once

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QQueue>
#include <QString>

class QSerialPort;
class QTimer;

namespace rr {

struct CwKeyerConfig {
    bool    enabled   = false;
    QString port;                        // port serie dedie a la manipulation
    QString line      = QStringLiteral("DTR");   // DTR ou RTS
    bool    inverted  = false;           // certaines interfaces inversent
    int     wpm       = 20;
    // Correction par element, en millisecondes. Un poste met un instant a
    // etablir sa porteuse : raccourcir chaque element compense ce retard.
    int     correctionMs = 0;
    // Maintenir le PTT pendant tout le message, pour les postes qui ne
    // commutent pas seuls sur la ligne de manipulation.
    bool    holdPtt   = false;
};

class CwKeyer : public QObject {
    Q_OBJECT
public:
    explicit CwKeyer(QObject *parent = nullptr);
    ~CwKeyer() override;

    bool busy() const { return !m_elements.isEmpty(); }

public slots:
    void open(const rr::CwKeyerConfig &cfg);
    void close();
    void send(const QString &text);
    void stop();
    void setWpm(int wpm);

signals:
    void pttRequested(bool on);
    void logMessage(const QString &message);
    void sendingChanged(bool sending);

private slots:
    void step();

private:
    void keyLine(bool down);
    void queueText(const QString &text);

    struct Element { bool down; int ms; };

    QSerialPort   *m_port = nullptr;
    QTimer        *m_timer = nullptr;
    QQueue<Element> m_elements;
    QElapsedTimer  m_clock;
    qint64         m_deadline = 0;       // echeance cumulee, en millisecondes
    CwKeyerConfig  m_cfg;
    bool           m_sending = false;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::CwKeyerConfig)

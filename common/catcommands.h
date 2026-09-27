// Commandes CAT decrites en donnees, non en code.
//
// Hamlib couvre l'essentiel du pilotage, mais laisse de cote quantite de
// reglages propres a chaque poste : gain micro, compresseur, notch, et surtout
// les menus internes. Les decrire dans un fichier plutot que dans du code
// permet d'ajouter un transceiver sans recompiler, et sans que le moteur sache
// quoi que ce soit du materiel.
//
// Voir data/cat/FORMAT.md pour la structure des fichiers.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace rr {

struct CatParamValue {
    QString value;   // insere tel quel dans la trame
    QString label;   // ce que lit l'operateur
    // A value the radio answers but does not accept: AGC answers AUTO-FAST,
    // AUTO-MID or AUTO-SLOW (4, 5, 6) where it is only set to AUTO (4).
    // Shown, never offered, never sent; "as" is the settable value it stands
    // for in a list.
    bool    readOnly = false;
    QString as;
};

struct CatParam {
    enum Type { Range, Enum, Text };

    QString id;
    QString name;
    Type    type = Range;

    // Plage
    int     min = 0;
    int     max = 0;
    int     digits = 1;     // largeur imposee par le poste, pas une coquetterie
    int     step = 1;
    bool    isSigned = false;  // le signe occupe un des caracteres
    QString unit;

    // Enumeration
    QList<CatParamValue> values;

    // Texte
    int     maxLength = 50;

    // FT891Remote: how the radio's step maps to what its display shows.
    // PITCH 00 is 300 Hz and each step adds 10 Hz; LCUT 00 is OFF. The
    // value sent is always the raw step; only the reading changes.
    double  dispOffset = 0.0;
    double  dispScale  = 1.0;
    int     dispDecimals = 0;
    QString dispUnit;          // empty: the plain unit above, if any
    QString zeroLabel;         // shown for the minimum, e.g. OFF
    QString describe(int step) const;

    // Rend la valeur au format attendu par le poste.
    QString format(const QString &raw) const;
    QString defaultValue() const;
};

struct CatCommand {
    QString code;           // identifiant dans le fichier, pas la trame
    QString name;
    QString setTemplate;    // vide si la commande est en lecture seule
    QString readTemplate;   // vide si le poste ne repond pas
    // The start of the answer, when it is not that of the set command: SPLIT
    // is set with ST but read with RI, which answers "RIC1".
    QString answer;
    // A setting shown with this switch, in the same row: the level or
    // frequency of the function it turns on (PRC with PRC-LVL).
    QString companion;
    QString note;
    QString tried;          // comment la commande a ete eprouvee
    // FT891Remote: a question the operator must answer before the command
    // is sent (a reset, a CAT rate change). Empty for ordinary settings.
    QString confirm;
    bool    verified = false;  // essayee sur le poste, au-dela du fichier
    QList<CatParam> params;

    bool canSet() const  { return !setTemplate.isEmpty(); }
    bool canRead() const { return !readTemplate.isEmpty(); }

    // Construit la trame a envoyer. values est indexe comme params.
    QString buildSet(const QStringList &values) const;

    // Decoupe la valeur rendue par le poste en une valeur par parametre.
    // Chaque parametre consomme sa propre largeur : « 1+0500 » se lit comme
    // « 1 » puis « +0500 » quand la commande porte un interrupteur et un
    // decalage signe.
    QStringList splitAnswer(const QString &value) const;
    QString buildRead() const { return readTemplate; }

    // FT891Remote: the part of an answer that precedes the parameters.
    //
    // It is taken from the set template, not from the read template: the
    // FT-891 answers « AC; » with « AC001; », and the tuner state is the
    // last digit, after the « AC00 » that the set template spells out.
    // Stripping only the read prefix « AC » would read « 0 » instead.
    QString answerPrefix() const;
};

struct CatGroup {
    QString name;
    // FT891Remote: how the server keeps the group up to date — "core",
    // "panel" (polled in rotation), "meter" or "menu" (read on demand).
    QString poll;
    bool    hidden = false;   // not shown as a page of its own
    QList<CatCommand> commands;
};

struct CatRig {
    QString rig;
    QString protocol;
    QString source;
    QString identify;
    bool    verified = false;   // essaye sur le materiel, ou seulement transcrit
    QList<int> hamlibModels;
    QList<CatGroup> groups;

    bool isValid() const { return !groups.isEmpty(); }
};

// Descriptions embarquees dans l'executable.
class CatLibrary {
public:
    // Tous les postes decrits, dans l'ordre alphabetique.
    static QStringList rigNames();

    // Par nom, ou par modele Hamlib — le serveur connait deja le poste.
    static CatRig byName(const QString &rig);
    static CatRig byHamlibModel(int model);

    // Par la reponse a « ID; ». Hamlib donne le meme numero de modele au
    // FT-991 et au FT-991A, alors que leurs menus divergent a partir du 088 :
    // seule cette reponse les departage.
    static CatRig byIdentify(const QString &id);

private:
    static const QList<CatRig> &all();
};

} // namespace rr

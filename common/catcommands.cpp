#include "catcommands.h"

#include <algorithm>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace rr {

namespace {

CatParam::Type typeFromName(const QString &s)
{
    if (s == QLatin1String("enum")) return CatParam::Enum;
    if (s == QLatin1String("text")) return CatParam::Text;
    return CatParam::Range;
}

CatParam readParam(const QJsonObject &o)
{
    CatParam p;
    p.id   = o.value("id").toString();
    p.name = o.value("name").toString();
    p.type = typeFromName(o.value("type").toString());
    p.min  = o.value("min").toInt(0);
    p.max  = o.value("max").toInt(0);
    p.digits = o.value("digits").toInt(1);
    p.step = o.value("step").toInt(1);
    p.isSigned = o.value("signed").toBool(false);
    p.unit = o.value("unit").toString();
    p.maxLength = o.value("maxLength").toInt(50);
    const QJsonObject disp = o.value("display").toObject();
    if (!disp.isEmpty()) {
        p.dispOffset   = disp.value("offset").toDouble(0.0);
        p.dispScale    = disp.value("scale").toDouble(1.0);
        p.dispDecimals = disp.value("decimals").toInt(0);
        p.dispUnit     = disp.value("unit").toString();
        p.zeroLabel    = disp.value("zero").toString();
    }
    const QJsonArray vals = o.value("values").toArray();
    for (const QJsonValue &v : vals) {
        const QJsonObject vo = v.toObject();
        CatParamValue pv;
        pv.value = vo.value("v").toString();
        pv.label = vo.value("label").toString();
        pv.readOnly = vo.value("readonly").toBool(false);
        pv.as = vo.value("as").toString();
        p.values.append(pv);
    }
    return p;
}

CatCommand readCommand(const QJsonObject &o)
{
    CatCommand c;
    c.code = o.value("code").toString();
    c.name = o.value("name").toString();
    c.setTemplate  = o.value("set").toString();
    c.readTemplate = o.value("read").toString();
    c.answer = o.value("answer").toString();
    c.companion = o.value("companion").toString();
    c.note = o.value("note").toString();
    c.tried = o.value("tried").toString();
    c.verified = o.value("verified").toBool(false);
    c.confirm = o.value("confirm").toString();
    const QJsonArray ps = o.value("params").toArray();
    for (const QJsonValue &v : ps) c.params.append(readParam(v.toObject()));
    return c;
}

CatRig readRig(const QByteArray &json, const QString &where)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("CAT description %s is not valid JSON: %s",
                 qPrintable(where), qPrintable(err.errorString()));
        return {};
    }
    const QJsonObject o = doc.object();

    CatRig r;
    r.rig      = o.value("rig").toString();
    r.protocol = o.value("protocol").toString();
    r.source   = o.value("source").toString();
    r.identify = o.value("identify").toString();
    r.verified = o.value("verified").toBool(false);
    const QJsonArray models = o.value("hamlibModels").toArray();
    for (const QJsonValue &v : models) r.hamlibModels.append(v.toInt());

    const QJsonArray groups = o.value("groups").toArray();
    for (const QJsonValue &g : groups) {
        const QJsonObject go = g.toObject();
        CatGroup grp;
        grp.name = go.value("name").toString();
        grp.poll = go.value("poll").toString(QStringLiteral("menu"));
        grp.hidden = go.value("hidden").toBool(false);
        const QJsonArray cmds = go.value("commands").toArray();
        for (const QJsonValue &c : cmds) grp.commands.append(readCommand(c.toObject()));
        if (!grp.commands.isEmpty()) r.groups.append(grp);
    }

    // Une description dont on ne sait pas parler le protocole ne sert a rien,
    // et laisser croire le contraire serait pire que de l'ignorer.
    if (r.protocol != QLatin1String("ascii-semicolon")) {
        qWarning("CAT description %s uses protocol '%s', which this build cannot "
                 "compose; ignored.", qPrintable(where), qPrintable(r.protocol));
        return {};
    }
    return r;
}

} // namespace

// ------------------------------------------------------------------ parametres
QString CatParam::format(const QString &raw) const
{
    switch (type) {
    case Enum:
        // La valeur porte sa propre largeur : on la copie sans y toucher.
        for (const CatParamValue &v : values)
            if (v.value == raw || v.label == raw) return v.value;
        return values.isEmpty() ? QString() : values.first().value;

    case Text:
        return raw.left(maxLength);

    case Range:
    default: {
        // La largeur est imposee par le poste : PC10; est rejete la ou
        // PC010; passe.
        const int v = qBound(min, raw.toInt(), max);
        if (!isSigned)
            return QStringLiteral("%1").arg(v, digits, 10, QLatin1Char('0'));
        // Parametre signe : le signe occupe un caractere, les autres portent
        // la valeur absolue. Le FT-891 ecrit « -00 » et non « +00 ».
        const QChar sign = v < 0 ? QLatin1Char('-') : QLatin1Char('+');
        return sign + QStringLiteral("%1").arg(qAbs(v), digits - 1, 10,
                                               QLatin1Char('0'));
    }
    }
}

QString CatParam::describe(int step) const
{
    if (!zeroLabel.isEmpty() && step == min) return zeroLabel;
    const QString unitText = !dispUnit.isEmpty() ? dispUnit
                           : (unit.contains(QLatin1Char('=')) ? QString() : unit);
    const double v = dispOffset + step * dispScale;
    QString s = QString::number(v, 'f', dispDecimals);
    if (isSigned && v > 0) s.prepend(QLatin1Char('+'));
    return unitText.isEmpty() ? s : s + QLatin1Char(' ') + unitText;
}

QString CatParam::defaultValue() const
{
    switch (type) {
    case Enum: return values.isEmpty() ? QString() : values.first().value;
    case Text: return QString();
    case Range:
    default:   return QString::number(min);
    }
}

// ------------------------------------------------------------------ commandes
QString CatCommand::buildSet(const QStringList &values) const
{
    QString out = setTemplate;
    for (int i = 0; i < params.size(); ++i) {
        const CatParam &p = params.at(i);
        const QString raw = i < values.size() ? values.at(i) : p.defaultValue();
        out.replace(QStringLiteral("{%1}").arg(p.id), p.format(raw));
    }
    return out;
}

QString CatCommand::answerPrefix() const
{
    auto strip = [](QString s) {
        while (s.endsWith(QLatin1Char(';')) || s.endsWith(QLatin1Char('\r'))) s.chop(1);
        return s;
    };
    if (!answer.isEmpty()) return answer;
    if (!params.isEmpty() && !setTemplate.isEmpty()) {
        const int brace = setTemplate.indexOf(QLatin1Char('{'));
        if (brace >= 0) return setTemplate.left(brace);
    }
    if (!readTemplate.isEmpty()) return strip(readTemplate);
    return strip(setTemplate);
}

QStringList CatCommand::splitAnswer(const QString &value) const
{
    QStringList out;
    int pos = 0;
    for (const CatParam &p : params) {
        int width = p.digits;
        if (p.type == CatParam::Enum) {
            // Les valeurs d'une enumeration portent leur propre largeur, qui
            // est la meme pour toutes.
            width = p.values.isEmpty() ? 1 : p.values.first().value.size();
        } else if (p.type == CatParam::Text) {
            width = value.size() - pos;          // le texte prend le reste
        }
        if (width <= 0 || pos >= value.size()) { out << QString(); continue; }
        out << value.mid(pos, width);
        pos += width;
    }
    return out;
}

// ------------------------------------------------------------------ catalogue
const QList<CatRig> &CatLibrary::all()
{
    static const QList<CatRig> rigs = [] {
        QList<CatRig> out;
        // Les descriptions sont embarquees : rien a installer, et cela vaut
        // aussi bien pour le telephone que pour le bureau.
        QDir dir(QStringLiteral(":/cat"));
        const QStringList files = dir.entryList(QStringList{"*.json"}, QDir::Files);
        for (const QString &name : files) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::ReadOnly)) continue;
            const CatRig r = readRig(f.readAll(), name);
            if (r.isValid()) out.append(r);
        }
        std::sort(out.begin(), out.end(),
                  [](const CatRig &a, const CatRig &b) { return a.rig < b.rig; });
        return out;
    }();
    return rigs;
}

QStringList CatLibrary::rigNames()
{
    QStringList out;
    for (const CatRig &r : all()) out << r.rig;
    return out;
}

CatRig CatLibrary::byName(const QString &rig)
{
    for (const CatRig &r : all())
        if (r.rig == rig) return r;
    return {};
}

CatRig CatLibrary::byHamlibModel(int model)
{
    for (const CatRig &r : all())
        if (r.hamlibModels.contains(model)) return r;
    return {};
}

CatRig CatLibrary::byIdentify(const QString &id)
{
    const QString want = id.trimmed();
    if (want.isEmpty()) return {};
    for (const CatRig &r : all())
        if (!r.identify.isEmpty() && r.identify == want) return r;
    return {};
}

} // namespace rr

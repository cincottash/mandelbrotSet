#include "Settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace {

// One table drives load and save, so the two never disagree. Unknown keys in the file are ignored and missing
// keys keep their defaults, so old or newer settings files never stop the app from starting.
template <typename F>
void fields(Settings& s, F&& f)
{
    f("language", s.language);
    f("theme", s.theme);
    f("folded_cards", s.foldedCards);
    f("cards_both_sides", s.cardsBothSides);
    f("always_on_top", s.alwaysOnTop);
    f("window_opacity", s.windowOpacity);
    f("julia_preview", s.juliaPreview);
    f("anti_aliasing", s.antiAliasing);
    f("geometry", s.geometry);
    f("zoom_speed_pct", s.zoomSpeedPct);
    f("session", s.session);
    f("last_folder", s.lastFolder);
    f("custom_palettes", s.customPalettes);
}

void get(const QJsonObject& o, const char* k, QJsonArray& v) { if (o.value(QLatin1String(k)).isArray()) v = o.value(QLatin1String(k)).toArray(); }
void put(QJsonObject& o, const char* k, const QJsonArray& v) { o[QLatin1String(k)] = v; }

void get(const QJsonObject& o, const char* k, QString& v) { if (o.value(QLatin1String(k)).isString()) v = o.value(QLatin1String(k)).toString(); }
void get(const QJsonObject& o, const char* k, bool& v) { if (o.value(QLatin1String(k)).isBool()) v = o.value(QLatin1String(k)).toBool(); }
void get(const QJsonObject& o, const char* k, int& v) { if (o.value(QLatin1String(k)).isDouble()) v = o.value(QLatin1String(k)).toInt(); }
void get(const QJsonObject& o, const char* k, QJsonObject& v) { if (o.value(QLatin1String(k)).isObject()) v = o.value(QLatin1String(k)).toObject(); }
void get(const QJsonObject& o, const char* k, QByteArray& v)
{
    if (o.value(QLatin1String(k)).isString()) v = QByteArray::fromBase64(o.value(QLatin1String(k)).toString().toLatin1());
}
void get(const QJsonObject& o, const char* k, QStringList& v)
{
    if (!o.value(QLatin1String(k)).isArray()) return;
    v.clear();
    for (const QJsonValue& x : o.value(QLatin1String(k)).toArray()) v << x.toString();
}

void put(QJsonObject& o, const char* k, const QString& v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, bool v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, int v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, const QJsonObject& v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, const QByteArray& v) { o[QLatin1String(k)] = QString::fromLatin1(v.toBase64()); }
void put(QJsonObject& o, const char* k, const QStringList& v) { o[QLatin1String(k)] = QJsonArray::fromStringList(v); }

}  // namespace

bool Settings::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    fields(*this, [&](const char* k, auto& v) { get(o, k, v); });
    // Sane even if the file was edited by hand.
    windowOpacity = std::clamp(windowOpacity, 30, 100);
    antiAliasing = std::clamp(antiAliasing, 1, 4);
    zoomSpeedPct = std::clamp(zoomSpeedPct, 25, 400);
    return true;
}

bool Settings::save(const QString& path) const
{
    QJsonObject o;
    fields(const_cast<Settings&>(*this), [&](const char* k, auto& v) { put(o, k, v); });
    QSaveFile f(path);   // never a half-written settings file
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

void Settings::resetView()
{
    const Settings d;
    theme = d.theme;
    foldedCards = d.foldedCards;
    cardsBothSides = d.cardsBothSides;
    alwaysOnTop = d.alwaysOnTop;
    windowOpacity = d.windowOpacity;
    juliaPreview = d.juliaPreview;
    antiAliasing = d.antiAliasing;
    zoomSpeedPct = d.zoomSpeedPct;
    geometry.clear();
}

// Color gradients for the coloring pass: a list of color stops, sampled into a table the shader repeats.
#pragma once

#include <QColor>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

struct Palette {
    QString name;
    QList<QPair<double, QColor>> stops;   // positions 0..1, sorted; the gradient wraps from the last to the first
    // `count` colors evenly across 0..1 (RGBA, 8 bits each), smooth and wrapping.
    QVector<quint32> sample(int count) const;
    QColor at(double t) const;
    void sortStops();
    QJsonArray stopsJson() const;                  // [[position, "#RRGGBB"], ...]
    static QList<QPair<double, QColor>> stopsFromJson(const QJsonArray& a);   // empty when unusable
};

// The built-in palettes, then the user's own.
const QList<Palette>& palettes();
const QList<Palette>& builtInPalettes();
const Palette& paletteByName(const QString& name);   // the first palette for an unknown name
bool hasPalette(const QString& name);
QStringList paletteNames();

// The user's own palettes (kept in the settings). Every change bumps paletteRevision(), so renderers know to
// upload the colors again even when the name stays.
const QList<Palette>& customPalettes();
void setCustomPalettes(const QList<Palette>& list);
void putCustomPalette(const Palette& p);             // adds it, or replaces the one with that name
void removeCustomPalette(const QString& name);
bool isCustomPalette(const QString& name);
bool isBuiltInPalette(const QString& name);
int paletteRevision();

}  // namespace core

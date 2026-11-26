// A saved place: the view, the formula and the colors, as a small JSON file anyone can open.
#pragma once

#include "ColorParams.h"
#include "Fractal.h"
#include "View.h"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace core {

struct Location {
    QString name;
    View view;
    FractalParams fractal;
    ColorParams colors;
    bool hasColors = true;     // built-in places keep the colors in use
    QImage thumbnail;          // small picture for the Places list (may be null)
    bool builtIn = false;

    QJsonObject toJson(bool withThumbnail = false) const;
    // False with a reason when the object isn't a location this app can use.
    bool fromJson(const QJsonObject& o, QString* error);

    bool save(const QString& path, QString* error) const;
    bool load(const QString& path, QString* error);
};

// Famous places to start from (they keep the colors in use).
QList<Location> builtInPlaces();
// The Gallery: famous fractals, each with its own look (they bring their colors along).
QList<Location> galleryPlaces();

}  // namespace core

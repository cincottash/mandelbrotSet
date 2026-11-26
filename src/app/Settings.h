// Everything the app remembers between runs, saved as JSON in the app data folder.
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct Settings {
    // View options (View > Reset View Options)
    QString theme = QStringLiteral("dark");
    QStringList foldedCards = {QStringLiteral("EDGES"), QStringLiteral("INTERIOR"), QStringLiteral("EFFECTS"),
                               QStringLiteral("SPHERE"), QStringLiteral("ACTIVITY")};
    bool alwaysOnTop = false;
    int windowOpacity = 100;          // 30..100 %
    bool juliaPreview = false;        // the Julia set of the point under the pointer, in a corner
    int antiAliasing = 2;             // samples per pixel side once the view rests: 1..4
    QByteArray geometry;

    // Exploring
    int zoomSpeedPct = 100;           // 25..400: how far one wheel step zooms

    // The last session, restored at start
    QJsonObject session;              // a location (view, formula, colors)

    // Files
    QString lastFolder;

    // The user's own palettes: [{"name": ..., "stops": [[position, "#RRGGBB"], ...]}, ...]
    QJsonArray customPalettes;

    bool load(const QString& path);
    bool save(const QString& path) const;
    void resetView();
};

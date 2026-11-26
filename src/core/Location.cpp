#include "Location.h"

#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace core {

namespace {
const QString kFormat = QStringLiteral("mandelbrotSet location");
constexpr int kVersion = 1;
}  // namespace

QJsonObject Location::toJson(bool withThumbnail) const
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), kFormat);
    o.insert(QStringLiteral("version"), kVersion);
    if (!name.isEmpty()) o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("view"), view.toJson());
    o.insert(QStringLiteral("fractal"), fractal.toJson());
    if (hasColors) o.insert(QStringLiteral("colors"), colors.toJson());
    if (withThumbnail && !thumbnail.isNull()) {
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        thumbnail.save(&b, "PNG");
        o.insert(QStringLiteral("thumbnail_png"), QString::fromLatin1(png.toBase64()));
    }
    return o;
}

bool Location::fromJson(const QJsonObject& o, QString* error)
{
    if (o.value(QStringLiteral("format")).toString() != kFormat) {
        if (error) *error = QStringLiteral("This isn't a location file.");
        return false;
    }
    if (o.value(QStringLiteral("version")).toInt(1) > kVersion) {
        if (error) *error = QStringLiteral("This location was saved by a newer version of the app.");
        return false;
    }
    Location l;
    l.name = o.value(QStringLiteral("name")).toString().left(200);
    if (!l.view.fromJson(o.value(QStringLiteral("view")).toObject(), error)) return false;
    l.fractal.fromJson(o.value(QStringLiteral("fractal")).toObject());
    l.hasColors = o.value(QStringLiteral("colors")).isObject();
    if (l.hasColors) l.colors.fromJson(o.value(QStringLiteral("colors")).toObject());
    const QString png = o.value(QStringLiteral("thumbnail_png")).toString();
    if (!png.isEmpty()) {
        QImage img;
        if (img.loadFromData(QByteArray::fromBase64(png.toLatin1()), "PNG") && img.width() <= 512 && img.height() <= 512)
            l.thumbnail = img;
    }
    *this = l;
    return true;
}

bool Location::save(const QString& path, QString* error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson(false)).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool Location::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (f.size() > 64 * 1024 * 1024) {
        if (error) *error = QStringLiteral("The file is too large to be a location.");
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        if (error) *error = QStringLiteral("This isn't a location file (it isn't valid JSON).");
        return false;
    }
    return fromJson(d.object(), error);
}

QList<Location> builtInPlaces()
{
    struct P { const char* name; const char* re; const char* im; const char* scale; double angle; bool julia;
               const char* jre; const char* jim; Formula formula = Formula::Classic; };
    static const P list[] = {
        {"Whole Set", "-0.6", "0", "1.25", 0, false, "", ""},
        {"Seahorse Valley", "-0.7435", "0.1314", "0.004", 0, false, "", ""},
        {"Elephant Valley", "0.2815", "0.0098", "0.012", 0, false, "", ""},
        {"Triple Spiral", "-0.0884", "0.6545", "0.012", 0, false, "", ""},
        {"Mini Mandelbrot", "-1.7548776662466927", "0", "0.025", 0, false, "", ""},
        {"Scepter Valley", "-1.3625", "0.0425", "0.018", 0, false, "", ""},
        {"Seahorse Spiral, 1e-12", "-0.743643887037158704752191506114774", "0.131825904205311970493132056385139", "3e-12", 0,
         false, "", ""},
        {"Seahorse Spiral, 1e-30", "-0.743643887037158704752191506114774", "0.131825904205311970493132056385139", "3e-30", 0,
         false, "", ""},
        {"Dendrite at i, 1e-100", "0", "1", "1e-100", 0, false, "", ""},
        {"Julia: Dragon", "0", "0", "1.25", 0, true, "-0.8", "0.156"},
        {"Julia: Douady Rabbit", "0", "0", "1.25", 0, true, "-0.123", "0.745"},
        {"Julia: Siegel Disk", "0", "0", "1.25", 0, true, "-0.390540870218399", "-0.586787907346969"},
        {"Julia: San Marco", "0", "0", "1.25", 0, true, "-0.75", "0"},
        {"Julia: Dendrite", "0", "0", "1.25", 0, true, "0", "1"},
        {"Julia: Spirals", "0", "0", "1.25", 0, true, "0.285", "0.01"},
        {"Burning Ship", "-0.45", "0.5", "1.25", 0, false, "", "", Formula::BurningShip},
        {"Burning Ship: The Ship", "-1.762", "0.034", "0.045", 0, false, "", "", Formula::BurningShip},
        {"Burning Ship: Julia", "0", "0", "1.25", 0, true, "-1.755", "0.03", Formula::BurningShip},
        {"Tricorn", "-0.2", "0", "1.25", 0, false, "", "", Formula::Tricorn},
    };
    QList<Location> out;
    for (const P& p : list) {
        Location l;
        l.name = QString::fromUtf8(p.name);
        QJsonObject v;
        v.insert(QStringLiteral("re"), QString::fromLatin1(p.re));
        v.insert(QStringLiteral("im"), QString::fromLatin1(p.im));
        v.insert(QStringLiteral("scale"), QString::fromLatin1(p.scale));
        v.insert(QStringLiteral("angle_degrees"), p.angle);
        QString err;
        l.view.fromJson(v, &err);
        l.fractal.type = p.julia ? FractalType::Julia : FractalType::Mandelbrot;
        l.fractal.formula = p.formula;
        if (p.julia) {
            l.fractal.juliaRe = QString::fromLatin1(p.jre);
            l.fractal.juliaIm = QString::fromLatin1(p.jim);
        }
        l.hasColors = false;
        l.builtIn = true;
        out << l;
    }
    {
        Location l;
        l.name = QStringLiteral("Buddhabrot");
        l.fractal.type = FractalType::Buddhabrot;
        l.view = l.fractal.home();
        l.hasColors = false;
        l.builtIn = true;
        out << l;
    }
    return out;
}

}  // namespace core

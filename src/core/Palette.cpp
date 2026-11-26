#include "Palette.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace core {

static Palette make(const char* name, std::initializer_list<const char*> colors)
{
    Palette p;
    p.name = QString::fromUtf8(name);
    const int n = int(colors.size());
    int i = 0;
    for (const char* c : colors) p.stops.append({double(i++) / n, QColor(QString::fromLatin1(c))});
    return p;
}

namespace {
QList<Palette>& customList()
{
    static QList<Palette> list;
    return list;
}
QList<Palette>& allList()
{
    static QList<Palette> list = builtInPalettes();
    return list;
}
int& revision()
{
    static int r = 0;
    return r;
}
void rebuildAll()
{
    allList() = builtInPalettes() + customList();
    ++revision();
}
}  // namespace

const QList<Palette>& palettes() { return allList(); }
const QList<Palette>& customPalettes() { return customList(); }
int paletteRevision() { return revision(); }

void setCustomPalettes(const QList<Palette>& list)
{
    customList().clear();
    for (const Palette& p : list)
        if (!p.name.trimmed().isEmpty() && p.stops.size() >= 2 && !isBuiltInPalette(p.name)) customList().append(p);
    rebuildAll();
}

void putCustomPalette(const Palette& p)
{
    if (p.name.trimmed().isEmpty() || p.stops.size() < 2 || isBuiltInPalette(p.name)) return;
    for (Palette& c : customList())
        if (c.name.compare(p.name, Qt::CaseInsensitive) == 0) {
            c = p;
            rebuildAll();
            return;
        }
    customList().append(p);
    rebuildAll();
}

void removeCustomPalette(const QString& name)
{
    customList().removeIf([&](const Palette& p) { return p.name.compare(name, Qt::CaseInsensitive) == 0; });
    rebuildAll();
}

bool isCustomPalette(const QString& name)
{
    for (const Palette& p : customList())
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool isBuiltInPalette(const QString& name)
{
    for (const Palette& p : builtInPalettes())
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool hasPalette(const QString& name) { return isBuiltInPalette(name) || isCustomPalette(name); }

void Palette::sortStops()
{
    for (auto& s : stops) s.first = std::clamp(s.first, 0.0, 1.0);
    std::stable_sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
}

QJsonArray Palette::stopsJson() const
{
    QJsonArray a;
    for (const auto& s : stops) a.append(QJsonArray{s.first, s.second.name(QColor::HexRgb).toUpper()});
    return a;
}

QList<QPair<double, QColor>> Palette::stopsFromJson(const QJsonArray& a)
{
    QList<QPair<double, QColor>> out;
    for (const QJsonValue& v : a) {
        const QJsonArray s = v.toArray();
        const double pos = s.at(0).toDouble(-1);
        const QColor c(s.at(1).toString());
        if (s.size() != 2 || !std::isfinite(pos) || pos < 0 || pos > 1 || !c.isValid()) return {};
        out.append({pos, c});
    }
    if (out.size() < 2 || out.size() > 64) return {};
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    return out;
}

const QList<Palette>& builtInPalettes()
{
    // Evenly spaced stops; the last color blends back into the first, so every gradient repeats seamlessly.
    static const QList<Palette> list = {
        make("Seahorse", {"#000764", "#206BCB", "#EDFFFF", "#FFAA00", "#000200"}),
        make("Lavender", {"#2A1B3D", "#6B4E9B", "#C9B6F2", "#F3EEFF", "#9A7FCF", "#4A3470"}),
        make("Nebula", {"#0B1A3A", "#1F4E8C", "#7FB8D8", "#F4E6A1", "#D9A441", "#5A3A1A", "#15244A"}),
        make("Electric", {"#FF2E63", "#FF9EC4", "#B8F3FF", "#08F7FE", "#0A9396", "#3A0CA3"}),
        make("Fire", {"#000000", "#5C0A00", "#C1272D", "#FF7F11", "#FFD23F", "#FFF8E1", "#3B0A00"}),
        make("Ice", {"#020B1F", "#0B3C5D", "#328CC1", "#8FE3F2", "#FFFFFF", "#5DA9E9", "#0B2545"}),
        make("Twilight", {"#1B1035", "#5B2A86", "#C04A9A", "#F28A54", "#FBD38D", "#7A3E8E"}),
        make("Ocean", {"#001219", "#005F73", "#0A9396", "#94D2BD", "#E9D8A6", "#0A9396", "#003845"}),
        make("Forest", {"#0B1D0E", "#1F4D2B", "#4F8A3C", "#B5C99A", "#F1E8B8", "#6B4F2A"}),
        make("Copper", {"#1A0F0A", "#5A2E1A", "#B87333", "#F0B27A", "#FFF1E0", "#8C4A2F"}),
        make("Sunset", {"#0D0221", "#4A0E4E", "#B21F66", "#FE346E", "#FF9A3C", "#FFD56B", "#38184C"}),
        make("Neon", {"#0A0A0A", "#39FF14", "#0A0A0A", "#FF073A", "#0A0A0A", "#00E5FF"}),
        make("Candy", {"#FFB5E8", "#B28DFF", "#85E3FF", "#BFFCC6", "#FFF5BA", "#FFABAB"}),
        make("Aurora", {"#03071E", "#0B6E4F", "#08A045", "#6BBF59", "#C2F970", "#1C7293", "#4B2C69"}),
        make("Gold", {"#0F0A00", "#4D3200", "#A67C00", "#FFD966", "#FFF6D5", "#B8860B", "#3D2B00"}),
        make("Magma", {"#000004", "#3B0F70", "#8C2981", "#DE4968", "#FE9F6D", "#FCFDBF"}),
        make("Emerald", {"#440154", "#3B528B", "#21918C", "#5EC962", "#FDE725"}),
        make("Grayscale", {"#000000", "#FFFFFF"}),
        make("Rainbow", {"#FF0000", "#FF8000", "#FFFF00", "#00FF00", "#00FFFF", "#0000FF", "#8000FF"}),
        make("Midnight", {"#000000", "#0D1B2A", "#1B263B", "#415A77", "#778DA9", "#E0E1DD", "#1B263B"}),
    };
    return list;
}

const Palette& paletteByName(const QString& name)
{
    for (const Palette& p : palettes())
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) return p;
    return palettes().front();
}

QStringList paletteNames()
{
    QStringList out;
    for (const Palette& p : palettes()) out << p.name;
    return out;
}

QColor Palette::at(double t) const
{
    if (stops.isEmpty()) return Qt::black;
    t -= std::floor(t);
    // Find the stops around t, wrapping past the last stop to the first.
    int i = int(stops.size()) - 1;
    for (int k = 0; k < stops.size(); ++k)
        if (stops[k].first <= t) i = k;
    const int j = (i + 1) % int(stops.size());
    double a = stops[i].first, b = stops[j].first;
    if (b <= a) b += 1.0;
    double tt = t;
    if (tt < a) tt += 1.0;
    double f = (b > a) ? (tt - a) / (b - a) : 0.0;
    f = f * f * (3.0 - 2.0 * f);   // smooth step: no visible kinks at the stops
    const QColor& c0 = stops[i].second;
    const QColor& c1 = stops[j].second;
    auto lerp = [f](double x, double y) { return x + (y - x) * f; };
    return QColor::fromRgbF(float(lerp(c0.redF(), c1.redF())), float(lerp(c0.greenF(), c1.greenF())),
                            float(lerp(c0.blueF(), c1.blueF())));
}

QVector<quint32> Palette::sample(int count) const
{
    QVector<quint32> out(count);
    for (int i = 0; i < count; ++i) {
        const QColor c = at(double(i) / count);
        // RGBA bytes in memory order (little-endian 32-bit: R in the lowest byte).
        out[i] = quint32(c.red()) | (quint32(c.green()) << 8) | (quint32(c.blue()) << 16) | (0xFFu << 24);
    }
    return out;
}

}  // namespace core

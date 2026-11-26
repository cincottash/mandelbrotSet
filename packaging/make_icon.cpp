// Renders resources/icon.svg to packaging/icon.ico (16-256 px, PNG-compressed entries) for the Windows exe.
// Build and run it once after changing the SVG; see the README ("Changing the icon").
#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QtEndian>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        qWarning("usage: make_icon <icon.svg> <icon.ico>");
        return 2;
    }
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) {
        qWarning("can't read %s", argv[1]);
        return 1;
    }
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    QList<QByteArray> pngs;
    for (int s : sizes) {
        QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        svg.render(&p);
        p.end();
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        pngs << png;
    }
    QByteArray ico;
    auto u16 = [&](quint16 v) { v = qToLittleEndian(v); ico.append(reinterpret_cast<const char*>(&v), 2); };
    auto u32 = [&](quint32 v) { v = qToLittleEndian(v); ico.append(reinterpret_cast<const char*>(&v), 4); };
    u16(0); u16(1); u16(quint16(pngs.size()));
    quint32 offset = 6 + 16 * quint32(pngs.size());
    for (int i = 0; i < pngs.size(); ++i) {
        const int s = sizes[i];
        ico.append(char(s >= 256 ? 0 : s));
        ico.append(char(s >= 256 ? 0 : s));
        ico.append(char(0));
        ico.append(char(0));
        u16(1); u16(32);
        u32(quint32(pngs[i].size()));
        u32(offset);
        offset += quint32(pngs[i].size());
    }
    for (const QByteArray& p : pngs) ico.append(p);
    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly) || out.write(ico) != ico.size()) {
        qWarning("can't write %s", argv[2]);
        return 1;
    }
    return 0;
}

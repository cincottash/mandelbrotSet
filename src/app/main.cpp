#include "AppData.h"
#include "FractalView.h"
#include "MainWindow.h"
#include "Settings.h"
#include "Themes.h"

#include "core/Location.h"
#include "render/GpuEngine.h"
#include "render/RenderCore.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QRegularExpression>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

void say(const QString& text) { fprintf(stderr, "%s\n", qPrintable(text)); }

// --render: a picture of a location file, without any window.
int renderFile(const QString& locationPath, const QString& out, const QString& size, int aa)
{
    core::Location loc;
    QString err;
    if (!loc.load(locationPath, &err)) {
        say(QStringLiteral("Can't open %1: %2").arg(locationPath, err));
        return 2;
    }
    int w = 1920, h = 1080;
    if (!size.isEmpty()) {
        const auto m = QRegularExpression(QStringLiteral(R"(^\s*(\d+)\s*[xX]\s*(\d+)\s*$)")).match(size);
        if (!m.hasMatch() || m.captured(1).toInt() < 1 || m.captured(2).toInt() < 1 || m.captured(1).toInt() > 32768
            || m.captured(2).toInt() > 32768) {
            say(QStringLiteral("The size must look like 3840x2160 (1 to 32768 pixels each way)."));
            return 2;
        }
        w = m.captured(1).toInt();
        h = m.captured(2).toInt();
    }
    QOpenGLContext ctx;
    QSurfaceFormat fmt;
    fmt.setVersion(4, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    ctx.setFormat(fmt);
    QOffscreenSurface surface;
    if (!ctx.create()) {
        say(QStringLiteral("No OpenGL context could be created."));
        return 3;
    }
    surface.setFormat(ctx.format());
    surface.create();
    if (!ctx.makeCurrent(&surface)) {
        say(QStringLiteral("The OpenGL context couldn't be used."));
        return 3;
    }
    render::GpuEngine gpu;
    if (!gpu.init(&err)) {
        say(err);
        return 3;
    }
    QImage img;
    {
        render::RenderCore core(gpu);
        core.setFractal(loc.fractal);
        core.setColors(loc.colors);
        int lastPct = -1;
        img = core.renderImage(loc.view, w, h, aa, &err, [&](double p) {
            const int pct = int(p * 100);
            if (pct / 10 != lastPct / 10) say(QStringLiteral("%1 %").arg(pct));
            lastPct = pct;
            return true;
        });
    }
    gpu.release();
    ctx.doneCurrent();
    if (img.isNull()) {
        say(QStringLiteral("The picture couldn't be rendered: %1").arg(err));
        return 4;
    }
    if (!img.save(out)) {
        say(QStringLiteral("Couldn't save %1.").arg(out));
        return 5;
    }
    say(QStringLiteral("Saved %1 (%2 x %3).").arg(QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath())).arg(w).arg(h));
    return 0;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(APP_NAME));
    QApplication::setApplicationDisplayName(QStringLiteral(APP_TITLE));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral(APP_ORG));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.svg")));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Explore the Mandelbrot and Julia sets, zooming as deep as you like."));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption dataFolder(QStringLiteral("data-folder"), QStringLiteral("Keep settings in FOLDER for this run."), QStringLiteral("FOLDER"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Save a picture of the window to FILE.png and quit (throwaway settings, never a dialog)."), QStringLiteral("FILE"));
    QCommandLineOption render(QStringLiteral("render"), QStringLiteral("Render the location file LOCATION to a picture (with --out) and quit."), QStringLiteral("LOCATION"));
    QCommandLineOption out(QStringLiteral("out"), QStringLiteral("The picture --render saves (.png or .jpg)."), QStringLiteral("FILE"));
    QCommandLineOption size(QStringLiteral("size"), QStringLiteral("The picture size for --render, like 3840x2160 (default 1920x1080)."), QStringLiteral("WxH"));
    QCommandLineOption aa(QStringLiteral("anti-aliasing"), QStringLiteral("Samples per pixel side for --render, 1 to 4 (default 2)."), QStringLiteral("N"), QStringLiteral("2"));
    cli.addOptions({dataFolder, screenshot, render, out, size, aa});
    cli.addPositionalArgument(QStringLiteral("location"), QStringLiteral("A location file to open."));
    cli.process(app);

    if (cli.isSet(render)) {
        if (!cli.isSet(out)) {
            say(QStringLiteral("--render needs --out FILE."));
            return 2;
        }
        return renderFile(cli.value(render), cli.value(out), cli.value(size), std::clamp(cli.value(aa).toInt(), 1, 4));
    }

    if (cli.isSet(screenshot)) {
        // Throwaway settings, never the user's, and never a dialog: problems go to the command line.
        QTemporaryDir tmp;
        appdata::resolve(tmp.path(), nullptr);
        Settings settings;
        themes::apply(themes::forChoice(settings.theme));
        MainWindow w(settings, false);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        if (!cli.positionalArguments().isEmpty()) {
            core::Location loc;
            QString err;
            if (!loc.load(cli.positionalArguments().first(), &err)) {
                say(QStringLiteral("Can't open %1: %2").arg(cli.positionalArguments().first(), err));
                return 2;
            }
            w.applyLocation(loc);
        }
        w.resize(1500, 920);
        w.show();
        int rc = 1;
        QTimer::singleShot(300, &w, [&] {
            w.view()->renderUntilIdle(20000);
            w.refreshStatus();
            rc =w.grab().save(cli.value(screenshot)) ? 0 : 1;
            if (rc) say(QStringLiteral("Couldn't save %1.").arg(cli.value(screenshot)));
            app.quit();
        });
        app.exec();
        return rc;
    }

    const QString folder = appdata::resolve(cli.value(dataFolder), nullptr);
    if (folder.isEmpty()) return 0;   // the first-run window was cancelled
    Settings settings;
    settings.load(appdata::settingsFile());
    themes::apply(themes::forChoice(settings.theme));
    MainWindow w(settings, true);
    if (!cli.positionalArguments().isEmpty()) w.openLocationFile(cli.positionalArguments().first());
    w.show();
    return app.exec();
}

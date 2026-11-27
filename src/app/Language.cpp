#include "Language.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMutex>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QTranslator>
#include <QVBoxLayout>

namespace language {

namespace {

// Reads src/app/i18n/<code>.json (built into the app). Each entry: {"en": ..., "es": ..., "note": ..., "context": ...}.
// "note" tells apart the same English used in different senses; "context" names a Qt class for Qt's own texts
// (the OK and Cancel buttons, for example); entries without one are the app's own text.
class JsonTranslator : public QTranslator {
public:
    bool loadJson(const QString& path, const QString& code)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : o.value(QStringLiteral("strings")).toArray()) {
            const QJsonObject e = v.toObject();
            const QString to = e.value(code).toString();
            if (to.isEmpty()) continue;
            m_map.insert(key(e.value(QStringLiteral("context")).toString().toUtf8(), e.value(QStringLiteral("en")).toString(),
                             e.value(QStringLiteral("note")).toString().toUtf8()),
                         to);
        }
        return !m_map.isEmpty();
    }

    QString translate(const char* context, const char* source, const char* disambiguation, int) const override
    {
        const QByteArray ctx = context && qstrcmp(context, "app") != 0 ? QByteArray(context) : QByteArray();
        const QString found = m_map.value(key(ctx, QString::fromUtf8(source), QByteArray(disambiguation)));
        if (found.isEmpty() && ctx.isEmpty()) {
            const QMutexLocker lock(&m_lock);   // render threads write log lines too
            m_missing.insert(QString::fromUtf8(source));
        }
        return found;   // empty: Qt shows the English
    }

    bool isEmpty() const override { return m_map.isEmpty(); }

    QStringList takeMissing()
    {
        const QMutexLocker lock(&m_lock);
        QStringList out(m_missing.begin(), m_missing.end());
        m_missing.clear();
        out.sort();
        return out;
    }

private:
    static QString key(const QByteArray& context, const QString& english, const QByteArray& note)
    {
        QString k = english;
        if (!note.isEmpty()) k.prepend(QString::fromUtf8(note) + QChar(4));
        if (!context.isEmpty()) k.prepend(QString::fromUtf8(context) + QChar(1));
        return k;
    }

    QHash<QString, QString> m_map;
    mutable QMutex m_lock;
    mutable QSet<QString> m_missing;
};

JsonTranslator* g_app = nullptr;
QTranslator* g_qt = nullptr;
QString g_current = QStringLiteral("en");

}  // namespace

QList<Choice> available()
{
    // Each name in its own language, so it can be found whatever the app shows now
    return {{QStringLiteral("en"), QStringLiteral("English")}, {QStringLiteral("es"), QStringLiteral("Español")}};
}

bool isKnown(const QString& code)
{
    for (const Choice& c : available())
        if (c.code == code) return true;
    return false;
}

QString nameOf(const QString& code)
{
    for (const Choice& c : available())
        if (c.code == code) return c.name;
    return code;
}

QString systemDefault()
{
    const QString sys = QLocale::languageToCode(QLocale::system().language());
    return isKnown(sys) ? sys : QStringLiteral("en");
}

void install(const QString& wanted)
{
    const QString code = isKnown(wanted) ? wanted : QStringLiteral("en");
    if (g_app) { QCoreApplication::removeTranslator(g_app); delete g_app; g_app = nullptr; }
    if (g_qt) { QCoreApplication::removeTranslator(g_qt); delete g_qt; g_qt = nullptr; }
    g_current = code;
    if (code == QLatin1String("en")) {
        QLocale::setDefault(QLocale::system());
        return;
    }
    // Qt's own windows and buttons (file dialogs, Yes/No, a text box's right-click menu), where the build has them
    auto* qt = new QTranslator;
    if (qt->load(QStringLiteral(":/i18n/qtbase_%1.qm").arg(code))) {
        QCoreApplication::installTranslator(qt);
        g_qt = qt;
    } else {
        delete qt;
    }
    auto* app = new JsonTranslator;
    if (app->loadJson(QStringLiteral(":/i18n/%1.json").arg(code), code)) {
        QCoreApplication::installTranslator(app);   // the last one installed is asked first
        g_app = app;
    } else {
        delete app;
    }
    // Day and month names in the language; numbers keep the system's country (es_US writes 1,234.5)
    QLocale::setDefault(QLocale(QLocale::codeToLanguage(code), QLocale::system().territory()));
}

QString current() { return g_current; }

QString ask(QWidget* parent, const QString& suggested)
{
    QDialog* d = makeAskDialog(parent, suggested);
    d->exec();   // closed without OK: the suggested language (already ticked)
    const QString code = chosen(d, suggested);
    delete d;
    return code;
}

QString chosen(QDialog* d, const QString& suggested)
{
    for (QRadioButton* r : d->findChildren<QRadioButton*>())
        if (r->isChecked() && isKnown(r->property("code").toString())) return r->property("code").toString();
    return isKnown(suggested) ? suggested : QStringLiteral("en");
}

QDialog* makeAskDialog(QWidget* parent, const QString& suggested)
{
    auto* dlg = new QDialog(parent);
    QDialog& d = *dlg;
    // Asked before any language is chosen: every line in every language
    d.setWindowTitle(QStringLiteral(APP_TITLE " · Language · Idioma"));
    auto* lay = new QVBoxLayout(&d);
    lay->setSpacing(10);
    auto* intro = new QLabel(QStringLiteral("Which language should %1 use?\n¿En qué idioma quieres usar %1?").arg(QStringLiteral(APP_TITLE)));
    lay->addWidget(intro);
    QList<QRadioButton*> buttons;
    for (const Choice& c : available()) {
        auto* r = new QRadioButton(c.name);
        r->setProperty("code", c.code);
        r->setProperty("testId", QStringLiteral("language-") + c.code);
        r->setChecked(c.code == suggested);
        buttons << r;
        lay->addWidget(r);
    }
    if (!isKnown(suggested)) buttons.first()->setChecked(true);
    auto* note = new QLabel(QStringLiteral("It can be changed later in File > Settings > Language.\n"
                                           "Se puede cambiar después en Archivo > Configuración > Idioma."));
    note->setObjectName(QStringLiteral("muted"));
    lay->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    lay->addWidget(box);
    d.setMinimumWidth(420);
    return dlg;
}

QStringList takeMissing() { return g_app ? g_app->takeMissing() : QStringList(); }

}  // namespace language

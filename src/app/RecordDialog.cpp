#include "RecordDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

RecordDialog::RecordDialog(const core::View& current, bool canLoop, const QString& whyNoLoop, const QSize& windowSize,
                           QWidget* parent)
    : QDialog(parent), m_current(current)
{
    setWindowTitle(QStringLiteral("Record Zoom"));
    auto* lay = new QVBoxLayout(this);
    auto* pathTitle = new QLabel(QStringLiteral("PATH"));
    pathTitle->setObjectName(QStringLiteral("cardTitle"));
    lay->addWidget(pathTitle);
    m_zoomIn = new QRadioButton(QStringLiteral("Zoom In From the Whole Set to This View"));
    m_zoomIn->setToolTip(QStringLiteral("Starts fully zoomed out and dives into the view on screen, which ends the movie."));
    m_loop = new QRadioButton(QStringLiteral("Seamless Loop Into a Mini Mandelbrot"));
    m_loop->setToolTip(QStringLiteral("Dives from the whole set into a tiny copy of it inside this view. The copy looks "
                                      "like the start, so the movie can repeat forever."));
    m_loop->setProperty("testId", QStringLiteral("loop"));
    auto* group = new QButtonGroup(this);
    group->addButton(m_zoomIn);
    group->addButton(m_loop);
    m_zoomIn->setChecked(true);
    lay->addWidget(m_zoomIn);
    lay->addWidget(m_loop);
    m_loopInfo = new QLabel;
    m_loopInfo->setWordWrap(true);
    m_loopInfo->setObjectName(QStringLiteral("muted"));
    m_loopInfo->setContentsMargins(26, 0, 0, 0);
    lay->addWidget(m_loopInfo);
    if (!canLoop) {
        m_loop->setEnabled(false);
        m_loopInfo->setText(whyNoLoop);
    }

    auto* formTitle = new QLabel(QStringLiteral("MOVIE"));
    formTitle->setObjectName(QStringLiteral("cardTitle"));
    lay->addSpacing(6);
    lay->addWidget(formTitle);
    auto* form = new QFormLayout;
    auto* secRow = new QHBoxLayout;
    m_seconds = new QDoubleSpinBox;
    m_seconds->setRange(1, 600);
    m_seconds->setDecimals(1);
    m_seconds->setSuffix(QStringLiteral(" s"));
    const double doublings = std::max(1.0, m_current.log10Magnification() * 3.3219);
    m_seconds->setValue(std::clamp(std::round(doublings), 5.0, 120.0));   // about one doubling per second
    secRow->addWidget(m_seconds);
    secRow->addStretch();
    form->addRow(QStringLiteral("Length"), secRow);
    m_fps = new QComboBox;
    m_fps->addItems({QStringLiteral("24"), QStringLiteral("30"), QStringLiteral("60")});
    m_fps->setCurrentIndex(1);
    auto* fpsRow = new QHBoxLayout;
    fpsRow->addWidget(m_fps);
    fpsRow->addWidget(new QLabel(QStringLiteral("Frames per Second")));
    fpsRow->addStretch();
    form->addRow(QStringLiteral("Frame Rate"), fpsRow);
    m_size = new QComboBox;
    const QSize win(windowSize.width() & ~1, windowSize.height() & ~1);
    m_size->addItem(QStringLiteral("Window Size (%1 × %2)").arg(win.width()).arg(win.height()), win);
    m_size->addItem(QStringLiteral("720p (1280 × 720)"), QSize(1280, 720));
    m_size->addItem(QStringLiteral("1080p (1920 × 1080)"), QSize(1920, 1080));
    m_size->addItem(QStringLiteral("1440p (2560 × 1440)"), QSize(2560, 1440));
    m_size->addItem(QStringLiteral("4K (3840 × 2160)"), QSize(3840, 2160));
    m_size->addItem(QStringLiteral("Square (1080 × 1080)"), QSize(1080, 1080));
    m_size->addItem(QStringLiteral("Custom"), QSize());
    m_size->setCurrentIndex(2);
    form->addRow(QStringLiteral("Size"), m_size);
    auto* wh = new QHBoxLayout;
    m_w = new QSpinBox;
    m_h = new QSpinBox;
    for (QSpinBox* b : {m_w, m_h}) {
        b->setRange(16, 8192);
        b->setSingleStep(2);
        b->setSuffix(QStringLiteral(" px"));
    }
    wh->addWidget(m_w);
    wh->addWidget(new QLabel(QStringLiteral("×")));
    wh->addWidget(m_h);
    wh->addStretch();
    form->addRow(QStringLiteral("Width and Height"), wh);
    m_aa = new QComboBox;
    m_aa->addItems({QStringLiteral("Off"), QStringLiteral("2 × 2"), QStringLiteral("3 × 3"), QStringLiteral("4 × 4")});
    m_aa->setCurrentIndex(1);
    m_aa->setToolTip(QStringLiteral("Samples per pixel: smoother edges and less shimmer between frames, for 4, 9 or 16 "
                                    "times the work."));
    form->addRow(QStringLiteral("Anti-Aliasing"), m_aa);
    m_format = new QComboBox;
    for (int i = 0; i < MovieWriter::formatNames().size(); ++i) {
        m_format->addItem(MovieWriter::formatNames()[i]);
        QString why;
        if (!MovieWriter::available(MovieWriter::Format(i), &why)) {
            m_format->setItemData(i, why, Qt::ToolTipRole);
            m_format->setItemData(i, 0, Qt::UserRole - 1);   // disabled entry
        }
    }
    m_format->setToolTip(QStringLiteral("MP4 plays everywhere; animated WebP loops by itself in browsers and chats; "
                                        "PNG Frames saves every frame as a picture in a folder."));
    form->addRow(QStringLiteral("Format"), m_format);
    lay->addLayout(form);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setObjectName(QStringLiteral("muted"));
    lay->addWidget(m_info);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(QStringLiteral("Record…"));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto applySize = [this] {
        const QSize s = m_size->currentData().toSize();
        const bool custom = !s.isValid();
        m_w->setEnabled(custom);
        m_h->setEnabled(custom);
        if (!custom) {
            m_w->setValue(s.width());
            m_h->setValue(s.height());
        }
        updateInfo();
    };
    applySize();
    connect(m_size, &QComboBox::currentIndexChanged, this, applySize);
    for (QSpinBox* b : {m_w, m_h}) connect(b, &QSpinBox::valueChanged, this, &RecordDialog::updateInfo);
    connect(m_seconds, &QDoubleSpinBox::valueChanged, this, &RecordDialog::updateInfo);
    connect(m_fps, &QComboBox::currentIndexChanged, this, &RecordDialog::updateInfo);
    connect(m_aa, &QComboBox::currentIndexChanged, this, &RecordDialog::updateInfo);
    connect(m_format, &QComboBox::currentIndexChanged, this, &RecordDialog::updateInfo);
    connect(m_loop, &QRadioButton::toggled, this, [this](bool on) {
        if (on && !m_minibrot.found && !m_searching) startSearch();
        updateInfo();
    });
    updateInfo();
    resize(520, sizeHint().height());
}

RecordDialog::~RecordDialog()
{
    if (m_cancel) m_cancel->store(true);
    if (m_watcher) m_watcher->waitForFinished();
}

void RecordDialog::startSearch()
{
    m_searching = true;
    m_loopInfo->setText(QStringLiteral("Looking for a mini Mandelbrot in this view…"));
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_watcher = new QFutureWatcher<core::Minibrot>(this);
    connect(m_watcher, &QFutureWatcher<core::Minibrot>::finished, this, [this] {
        m_searching = false;
        m_minibrot = m_watcher->result();
        if (m_minibrot.found) {
            const double doublings = -m_minibrot.size.log2Abs();
            m_loopInfo->setText(QStringLiteral("Found one: period %1, about %2 across. The loop zooms %3 times in "
                                               "(%4 halvings of the view).")
                                    .arg(QLocale().toString(m_minibrot.period))
                                    .arg(core::formatFloatExp(m_minibrot.size.abs(), 2))
                                    .arg(core::formatFloatExp(core::FloatExp(1.0) / m_minibrot.size.abs(), 2))
                                    .arg(QLocale().toString(doublings, 'f', 1)));
            m_seconds->setValue(std::clamp(std::round(doublings * 1.2), 5.0, 600.0));
        } else {
            m_loopInfo->setText(QStringLiteral("No mini Mandelbrot in this view. Zoom closer to one (the small black "
                                               "copies of the set) and open Record Zoom again."));
        }
        updateInfo();
    });
    const core::View v = m_current;
    auto cancel = m_cancel;
    m_watcher->setFuture(QtConcurrent::run([v, cancel] { return core::findMinibrot(v, 400000, cancel.get()); }));
    updateInfo();
}

void RecordDialog::updateInfo()
{
    const RecordSettings s = settings();
    const bool loopReady = !s.loop || m_minibrot.found;
    const double doublings = s.loop ? (m_minibrot.found ? -m_minibrot.size.log2Abs() : 0)
                                    : std::max(0.0, m_current.log10Magnification() * 3.3219);
    QStringList parts;
    parts << QStringLiteral("%1 frames").arg(QLocale().toString(s.frames()));
    if (doublings > 0)
        parts << QStringLiteral("the view halves %1 times a second").arg(QLocale().toString(doublings / s.seconds, 'f', 2));
    if (s.format == MovieWriter::Format::WebP && s.size.width() * s.size.height() > 1280 * 720)
        parts << QStringLiteral("animated WebP files grow large above 720p");
    if (s.format == MovieWriter::Format::Mp4 && (s.size.width() > 4096 || s.size.height() > 2304))
        parts << QStringLiteral("MP4 works up to 4096 × 2304");
    m_info->setText(parts.join(QStringLiteral(" · ")) + QStringLiteral("."));
    const bool mp4TooBig = s.format == MovieWriter::Format::Mp4 && (s.size.width() > 4096 || s.size.height() > 2304);
    m_ok->setEnabled(loopReady && !m_searching && !mp4TooBig);
    m_ok->setToolTip(m_searching ? QStringLiteral("Still looking for a mini Mandelbrot.")
                     : !loopReady ? QStringLiteral("The loop needs a mini Mandelbrot in the view.")
                     : mp4TooBig ? QStringLiteral("MP4 works up to 4096 × 2304; choose a smaller size or PNG Frames.")
                                 : QString());
}

RecordSettings RecordDialog::settings() const
{
    RecordSettings s;
    s.loop = m_loop->isChecked();
    s.seconds = m_seconds->value();
    s.fps = m_fps->currentText().toInt();
    s.size = QSize(m_w->value() & ~1, m_h->value() & ~1);
    s.samplesPerSide = m_aa->currentIndex() + 1;
    s.format = MovieWriter::Format(m_format->currentIndex());
    s.minibrot = m_minibrot;
    if (s.loop && m_minibrot.found) {
        s.from = core::View::home(false);
        s.to = core::minibrotHome(m_minibrot);
    } else {
        s.from = core::View::home(false);
        s.to = m_current;
    }
    return s;
}

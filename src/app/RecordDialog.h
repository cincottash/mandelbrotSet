// Record Zoom: the choices for a zoom movie (path, length, frame rate, size, format).
#pragma once

#include "MovieWriter.h"

#include "core/Minibrot.h"
#include "core/View.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QSize>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QRadioButton;

struct RecordSettings {
    bool loop = false;              // a seamless loop into a mini Mandelbrot, or a zoom into the current view
    double seconds = 20;
    int fps = 30;
    QSize size{1920, 1080};
    int samplesPerSide = 2;
    MovieWriter::Format format = MovieWriter::Format::Mp4;
    core::View from, to;            // the path's ends (the loop's "to" is the minibrot's copy of the start)
    core::Minibrot minibrot;
    double crossfadeSeconds = 0.75; // loops only
    bool keyframes = true;          // frames from a keyframe per halving (fast); false: every frame computed in full
    int frames() const { return std::max(2, int(std::lround(seconds * fps))); }
};

class RecordDialog : public QDialog {
    Q_OBJECT
public:
    // `canLoop` false (Julia sets, powers above 2) explains why the loop isn't offered.
    RecordDialog(const core::View& current, bool canLoop, const QString& whyNoLoop, const QSize& windowSize, QWidget* parent);
    ~RecordDialog() override;
    RecordSettings settings() const;

private:
    void updateInfo();
    void startSearch();

    core::View m_current;
    QRadioButton* m_zoomIn = nullptr;
    QRadioButton* m_loop = nullptr;
    QLabel* m_loopInfo = nullptr;
    QDoubleSpinBox* m_seconds = nullptr;
    QComboBox* m_fps = nullptr;
    QComboBox* m_size = nullptr;
    QSpinBox* m_w = nullptr;
    QSpinBox* m_h = nullptr;
    QComboBox* m_aa = nullptr;
    QCheckBox* m_fullQuality = nullptr;
    QComboBox* m_format = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_ok = nullptr;
    core::Minibrot m_minibrot;
    bool m_searching = false;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QFutureWatcher<core::Minibrot>* m_watcher = nullptr;
};

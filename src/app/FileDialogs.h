// Every file dialog goes through here: below 100 % Window Opacity, Qt's own dialog (which can be see-through and
// follows the theme); at 100 %, the system's.
#pragma once

#include <QFileDialog>

namespace filedialogs {
inline int& opacityPercent()
{
    static int pct = 100;
    return pct;
}
inline QFileDialog::Options options(QFileDialog::Options base = {})
{
    if (opacityPercent() < 100) base |= QFileDialog::DontUseNativeDialog;
    return base;
}
}  // namespace filedialogs

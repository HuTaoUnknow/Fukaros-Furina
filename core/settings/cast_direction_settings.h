#pragma once
#include <QSettings>

// Keep direction separate from editable connection settings. Saving it must not
// accept unfinished changes in an open settings dialog.
inline bool furinaRestoreCastDirection(bool fallbackSharing) {
    const auto value = QSettings().value("castDirection").toString();
    if (value == "share")
        return true;
    if (value == "receive")
        return false;
    return fallbackSharing;
}

inline void furinaSaveCastDirection(bool sharing) {
    QSettings settings;
    settings.setValue("castDirection", sharing ? "share" : "receive");
    // Android may reclaim the process after it enters the background without
    // running the C++ window destructor.
    settings.sync();
}

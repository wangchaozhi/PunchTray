#pragma once

#include <QString>

struct Config
{
    QString url;
    QString cookie;
    QString staffId;
    QString staffText;
    int intervalMinutes = 10;   // polling interval
    int daysBack = 0;           // 0 = today only
    bool autoPopup = true;      // pop the panel when new missed punches appear

    static QString filePath();
    static Config load();       // creates a template file on first run
    bool save() const;
    bool isComplete() const { return !url.isEmpty() && !cookie.isEmpty() && !staffId.isEmpty(); }
};

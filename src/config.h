#pragma once

#include <QPoint>
#include <QString>

#include <optional>

struct Config
{
    QString url;
    QString cookie;
    QString staffId;
    QString staffText;
    int intervalMinutes = 10;   // polling interval
    int daysBack = 0;           // 0 = today only
    bool autoPopup = true;      // pop the panel when new missed punches appear
    bool showSeconds = true;    // live clock shows HH:mm:ss instead of HH:mm
    bool autoUpdate = true;     // check GitHub Releases in the background
    QString githubToken;        // only needed while the repository is private
    std::optional<QPoint> panelPos; // dragged card position; unset = top-right corner

    static QString filePath();
    static Config load();       // creates a template file on first run
    bool save() const;
    bool isComplete() const { return !url.isEmpty() && !cookie.isEmpty() && !staffId.isEmpty(); }
};

#pragma once

#include "attendanceclient.h"
#include "config.h"

#include <QObject>
#include <QSet>

class GlassPanel;
class QSystemTrayIcon;
class QTimer;

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(QObject *parent = nullptr);
    ~TrayApp() override;

    void showPanel();

private:
    void refresh();
    void openSettings();
    void onRecords(const QList<AttendanceRecord> &records);
    void onFailed(const QString &message);
    void updateIcon(int missing, bool error);

    Config m_config;
    AttendanceClient *m_client;
    GlassPanel *m_panel;
    QSystemTrayIcon *m_tray;
    QTimer *m_timer;
    QSet<QString> m_seen; // reasons already popped up, so auto-popup fires once per new item
};

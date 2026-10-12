#pragma once

#include "attendanceclient.h"
#include "config.h"

#include <QObject>
#include <QSet>

class GlassPanel;
class QMenu;
class QSystemTrayIcon;
class QAction;
class QTimer;
class Updater;

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(QObject *parent = nullptr);
    ~TrayApp() override;

    void showPanel();
    void updateNow(); // check now and install a newer release without asking

private:
    void refresh();
    void openSettings();
    void onRecords(const QList<AttendanceRecord> &records);
    void onFailed(const QString &message);
    void updateIcon(int missing, bool error);
    void setupUpdater(QMenu *menu);

    Config m_config;
    AttendanceClient *m_client;
    GlassPanel *m_panel;
    QSystemTrayIcon *m_tray;
    QTimer *m_timer;
    Updater *m_updater;
    QAction *m_updateAction = nullptr;
    bool m_installWhenFound = false;
    QTimer *m_updateTimer;
    QSet<QString> m_seen; // reasons already popped up, so auto-popup fires once per new item
};

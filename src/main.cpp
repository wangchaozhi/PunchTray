#include "trayapp.h"

#include <QApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PunchTray"));
    QApplication::setOrganizationName(QStringLiteral("PunchTray"));
    QApplication::setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("PunchTray"),
                              QStringLiteral("当前桌面环境不支持系统托盘。"));
        return 1;
    }

    TrayApp tray;
    return app.exec();
}

#include "trayapp.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSystemTrayIcon>

namespace {
// One name per user, so different accounts on the same machine each get their own instance.
QString instanceName()
{
    const QByteArray home = QDir::homePath().toUtf8();
    return QStringLiteral("PunchTray-")
        + QString::fromLatin1(QCryptographicHash::hash(home, QCryptographicHash::Sha1).toHex().left(16));
}

// Asks a running instance to show its panel. Returns true when one answered.
bool signalRunningInstance(const QString &name)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(500))
        return false;
    socket.write("show\n");
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PunchTray"));
    QApplication::setOrganizationName(QStringLiteral("PunchTray"));
    QApplication::setQuitOnLastWindowClosed(false);

    const QString name = instanceName();
    if (signalRunningInstance(name))
        return 0;

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(name)) {
        // A crashed instance can leave a stale socket file behind (Unix).
        QLocalServer::removeServer(name);
        server.listen(name);
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("PunchTray"),
                              QStringLiteral("当前桌面环境不支持系统托盘。"));
        return 1;
    }

    TrayApp tray;
    QObject::connect(&server, &QLocalServer::newConnection, &tray, [&server, &tray] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            tray.showPanel();
        }
    });
    return app.exec();
}

#include "trayapp.h"
#include "updater.h"

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

// Passes a command ("show" or "update") to a running instance. Returns true when one answered.
bool signalRunningInstance(const QString &name, const QByteArray &command)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(500))
        return false;
    socket.write(command + '\n');
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
    QApplication::setApplicationVersion(Updater::currentVersion());
    QApplication::setQuitOnLastWindowClosed(false);

    // --update: check GitHub Releases now and install a newer version without asking.
    const bool updateNow = app.arguments().contains(QStringLiteral("--update"));

    const QString name = instanceName();
    if (signalRunningInstance(name, updateNow ? "update" : "show"))
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
            QObject::connect(client, &QLocalSocket::readyRead, &tray, [client, &tray] {
                while (client->canReadLine()) {
                    if (client->readLine().trimmed() == "update")
                        tray.updateNow();
                    else
                        tray.showPanel();
                }
            });
        }
    });
    if (updateNow)
        tray.updateNow();
    return app.exec();
}

#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
const char *kName = "PunchTray";

QString executable()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

#if defined(Q_OS_WIN)
const char *kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#elif defined(Q_OS_MACOS)
QString agentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/com.local.punchtray.plist";
}
#else
QString desktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/autostart/punchtray.desktop";
}
#endif

bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(content) == content.size();
}
} // namespace

bool AutoStart::isEnabled()
{
#if defined(Q_OS_WIN)
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.value(kName).toString().contains(executable(), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
    return QFile::exists(agentPath());
#else
    return QFile::exists(desktopPath());
#endif
}

bool AutoStart::setEnabled(bool enabled)
{
#if defined(Q_OS_WIN)
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled)
        run.setValue(kName, QStringLiteral("\"%1\"").arg(executable()));
    else
        run.remove(kName);
    run.sync();
    return run.status() == QSettings::NoError;
#elif defined(Q_OS_MACOS)
    if (!enabled)
        return !QFile::exists(agentPath()) || QFile::remove(agentPath());
    const QString plist = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>com.local.punchtray</string>\n"
        "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "</dict></plist>\n").arg(executable().toHtmlEscaped());
    return writeFile(agentPath(), plist.toUtf8());
#else
    if (!enabled)
        return !QFile::exists(desktopPath()) || QFile::remove(desktopPath());
    const QString entry = QStringLiteral(
        "[Desktop Entry]\nType=Application\nName=PunchTray\nExec=\"%1\"\n"
        "X-GNOME-Autostart-enabled=true\n").arg(executable());
    return writeFile(desktopPath(), entry.toUtf8());
#endif
}

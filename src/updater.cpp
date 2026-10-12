#include "updater.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>

#include <cstdio>

#ifndef PUNCHTRAY_VERSION
#define PUNCHTRAY_VERSION "0.0.0"
#endif

namespace {
const char *kLatestReleaseApi = "https://api.github.com/repos/wangchaozhi/PunchTray/releases/latest";

// Must match the asset names produced by .github/workflows/release.yml.
QString assetSuffix()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("-windows-x64.zip");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("-macos-arm64.dmg");
#else
    return QStringLiteral("-linux-x64.AppImage");
#endif
}

#if defined(Q_OS_MACOS)
// The running .app bundle, or empty when not launched from one.
QString bundlePath()
{
    QDir dir(QCoreApplication::applicationDirPath()); // .../PunchTray.app/Contents/MacOS
    if (!dir.cdUp() || !dir.cdUp() || !dir.absolutePath().endsWith(QLatin1String(".app")))
        return {};
    return dir.absolutePath();
}
#endif

bool writeScript(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(content);
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    return true;
}
} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString Updater::currentVersion()
{
    return QStringLiteral(PUNCHTRAY_VERSION);
}

QNetworkReply *Updater::get(const QUrl &url, bool authenticate, const QByteArray &accept)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "PunchTray/" PUNCHTRAY_VERSION);
    req.setRawHeader("Accept", accept);
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // Redirects are followed by hand so the token never reaches the storage host.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(30000);
    if (authenticate && !m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    return m_nam->get(req);
}

void Updater::check(bool manual)
{
    if (m_busy)
        return;
    m_busy = true;
    QNetworkReply *reply = get(QUrl(QString::fromLatin1(kLatestReleaseApi)), true, "application/vnd.github+json");
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            emit checkFailed(m_token.isEmpty() ? QStringLiteral("找不到发布版本（私有仓库需要在设置里填写 GitHub Token）")
                                               : QStringLiteral("找不到发布版本"),
                             manual);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(QStringLiteral("检查更新失败：%1").arg(reply->errorString()), manual);
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        ReleaseInfo r;
        r.tag = o.value("tag_name").toString();
        QString v = r.tag;
        if (v.startsWith('v') || v.startsWith('V'))
            v.remove(0, 1);
        r.version = QVersionNumber::fromString(v);
        r.pageUrl = QUrl(o.value("html_url").toString());
        for (const QJsonValue &a : o.value("assets").toArray()) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value("name").toString();
            if (name.endsWith(assetSuffix(), Qt::CaseInsensitive)) {
                r.assetName = name;
                r.assetUrl = QUrl(asset.value("browser_download_url").toString());
                r.assetApiUrl = QUrl(asset.value("url").toString());
                r.assetSize = asset.value("size").toInteger();
                break;
            }
        }
        if (r.version.isNull()) {
            emit checkFailed(QStringLiteral("无法解析发布版本号"), manual);
            return;
        }

        m_latest = r;
        m_hasUpdate = r.version > QVersionNumber::fromString(currentVersion());
        if (m_hasUpdate)
            emit updateAvailable(r, manual);
        else
            emit upToDate(manual);
    });
}

QString Updater::downloadTarget() const
{
#if defined(Q_OS_LINUX)
    // Same directory as the AppImage so the final rename is atomic.
    return qEnvironmentVariable("APPIMAGE") + QStringLiteral(".new");
#else
    const QString dir = QDir::temp().filePath(QStringLiteral("PunchTray-update"));
    QDir().mkpath(dir);
    return QDir(dir).filePath(m_latest.assetName);
#endif
}

void Updater::install()
{
    if (m_busy || !m_hasUpdate)
        return;

    bool selfInstall = !m_latest.assetName.isEmpty();
#if defined(Q_OS_LINUX)
    selfInstall = selfInstall && qEnvironmentVariableIsSet("APPIMAGE");
#elif defined(Q_OS_MACOS)
    selfInstall = selfInstall && !bundlePath().isEmpty();
#endif
    if (!selfInstall) {
        // Not a packaged install we know how to replace; let the user download it.
        QDesktopServices::openUrl(m_latest.pageUrl);
        return;
    }

    m_file = new QFile(downloadTarget(), this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit installFailed(QStringLiteral("无法写入 %1").arg(m_file->fileName()));
        m_file->deleteLater();
        m_file = nullptr;
        return;
    }
    m_busy = true;
    // Public repositories serve the browser URL anonymously; private ones need the
    // authenticated API endpoint.
    if (m_token.isEmpty())
        download(m_latest.assetUrl, false, 0);
    else
        download(m_latest.assetApiUrl, true, 0);
}

void Updater::download(const QUrl &url, bool authenticate, int hops)
{
    QNetworkReply *reply = get(url, authenticate, "application/octet-stream");
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200)
            m_file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 got, qint64 total) {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        if (total <= 0)
            total = m_latest.assetSize;
        if (total > 0)
            emit downloadProgress(int(got * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, hops] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400 && hops < 5) {
            const QUrl next = reply->url().resolved(
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            download(next, false, hops + 1);
            return;
        }

        auto fail = [this](const QString &message) {
            m_busy = false;
            m_file->remove();
            m_file->deleteLater();
            m_file = nullptr;
            emit installFailed(message);
        };
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            fail(QStringLiteral("下载更新失败：%1").arg(reply->errorString()));
            return;
        }
        m_file->write(reply->readAll());
        m_file->close();
        if (m_latest.assetSize > 0 && m_file->size() != m_latest.assetSize) {
            fail(QStringLiteral("下载的文件大小不对，已放弃更新"));
            return;
        }
        applyDownload();
    });
}

void Updater::applyDownload()
{
    const QString package = m_file->fileName();
    m_file->deleteLater();
    m_file = nullptr;
    m_busy = false;

    const QString pid = QString::number(QCoreApplication::applicationPid());
    bool started = false;

#if defined(Q_OS_WIN)
    // Wait for this process to exit, unpack over the install folder, relaunch.
    const QString script = QDir::temp().filePath(QStringLiteral("PunchTray-update/apply-update.ps1"));
    const QByteArray ps1 = R"PS(param([int]$ProcId, [string]$Zip, [string]$Dest, [string]$Exe)
$ErrorActionPreference = 'Stop'
try {
    try { Wait-Process -Id $ProcId -Timeout 30 } catch {}
    Start-Sleep -Milliseconds 300
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("PunchTray-unpack-" + [guid]::NewGuid())
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
    Copy-Item -Path (Join-Path $tmp '*') -Destination $Dest -Recurse -Force
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Remove-Item -LiteralPath $Zip -Force
} finally {
    Start-Process -FilePath $Exe
}
)PS";
    if (writeScript(script, ps1)) {
        started = QProcess::startDetached(
            QStringLiteral("powershell.exe"),
            {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script,
             "-ProcId", pid, "-Zip", QDir::toNativeSeparators(package),
             "-Dest", QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
             "-Exe", QDir::toNativeSeparators(QCoreApplication::applicationFilePath())});
    }
#elif defined(Q_OS_MACOS)
    const QString script = QDir::temp().filePath(QStringLiteral("PunchTray-update/apply-update.sh"));
    const QByteArray sh = R"SH(#!/bin/sh
pid="$1"; dmg="$2"; app="$3"
while kill -0 "$pid" 2>/dev/null; do sleep 0.3; done
mnt="$(mktemp -d)"
if hdiutil attach -nobrowse -quiet -mountpoint "$mnt" "$dmg"; then
    src="$(find "$mnt" -maxdepth 2 -name '*.app' -type d | head -n 1)"
    if [ -n "$src" ]; then
        rm -rf "$app.old"
        if mv "$app" "$app.old"; then
            if ditto "$src" "$app"; then rm -rf "$app.old"; else rm -rf "$app"; mv "$app.old" "$app"; fi
        fi
    fi
    hdiutil detach -quiet "$mnt"
fi
rm -f "$dmg"
open "$app"
)SH";
    if (writeScript(script, sh))
        started = QProcess::startDetached(QStringLiteral("/bin/sh"), {script, pid, package, bundlePath()});
#else
    // An AppImage can be replaced while running: swap the file, start the new one.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    QFile::setPermissions(package, QFile::permissions(package) | QFileDevice::ExeOwner
                                       | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    if (::rename(QFile::encodeName(package).constData(), QFile::encodeName(appImage).constData()) == 0) {
        // The new process will find our single-instance socket still open, so wait for
        // this one to exit first.
        started = QProcess::startDetached(
            QStringLiteral("/bin/sh"),
            {"-c", QStringLiteral("while kill -0 %1 2>/dev/null; do sleep 0.3; done; exec \"$0\"").arg(pid),
             appImage});
    }
#endif

    if (!started) {
        QFile::remove(package);
        emit installFailed(QStringLiteral("无法启动更新程序"));
        return;
    }
    QCoreApplication::quit();
}

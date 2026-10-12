#pragma once

#include <QObject>
#include <QUrl>
#include <QVersionNumber>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

struct ReleaseInfo
{
    QString tag;              // e.g. "v1.0.1"
    QVersionNumber version;
    QUrl pageUrl;             // release page, used when we cannot self-install
    QString assetName;
    QUrl assetUrl;            // browser_download_url
    QUrl assetApiUrl;         // API asset url (needed for private repositories)
    qint64 assetSize = 0;
};

// Checks GitHub Releases for a newer build and replaces the running installation:
// Windows portable folder (zip), macOS .app bundle (dmg), Linux AppImage.
class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(QObject *parent = nullptr);

    static QString currentVersion();

    void setToken(const QString &token) { m_token = token; }
    void check(bool manual);
    void install();             // downloads and applies latest(); quits the app on success
    bool hasUpdate() const { return m_hasUpdate; }
    const ReleaseInfo &latest() const { return m_latest; }
    bool busy() const { return m_busy; }

signals:
    void updateAvailable(const ReleaseInfo &release, bool manual);
    void upToDate(bool manual);
    void checkFailed(const QString &message, bool manual);
    void downloadProgress(int percent);
    void installFailed(const QString &message);

private:
    QNetworkReply *get(const QUrl &url, bool authenticate, const QByteArray &accept);
    void download(const QUrl &url, bool authenticate, int hops);
    void applyDownload();
    QString downloadTarget() const;

    QNetworkAccessManager *m_nam;
    QString m_token;
    ReleaseInfo m_latest;
    bool m_hasUpdate = false;
    bool m_busy = false;
    QFile *m_file = nullptr;
};

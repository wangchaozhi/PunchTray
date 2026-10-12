#pragma once

#include "config.h"

#include <QDate>
#include <QList>
#include <QObject>

class QNetworkAccessManager;

struct AttendanceRecord
{
    QDate date;
    QString dateText;     // e.g. "2026/10/12 星期一"
    QString firstCard;    // actual first punch, may be empty
    QString lastCard;     // actual last punch, may be empty
    QString reason;       // e.g. "18:58未打卡"
    QStringList missedTimes; // times extracted from reason, e.g. "18:58"
};

class AttendanceClient : public QObject
{
    Q_OBJECT
public:
    explicit AttendanceClient(QObject *parent = nullptr);

    void fetch(const Config &config);

    // Exposed for testing the parser against saved responses.
    static QList<AttendanceRecord> parse(const QByteArray &json, QString *error);

signals:
    void finished(const QList<AttendanceRecord> &missing);
    void failed(const QString &message);

private:
    QByteArray buildBody(const Config &config) const;

    QNetworkAccessManager *m_nam;
    bool m_busy = false;
};

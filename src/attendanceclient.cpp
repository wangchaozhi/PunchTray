#include "attendanceclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

AttendanceClient::AttendanceClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

static QJsonObject searchItem(const QString &field, const QString &text, const QString &value,
                              const QString &num)
{
    return QJsonObject{
        {"name", "Attendance.AttendanceStatistics." + field},
        {"text", text},
        {"value", value},
        {"num", num},
        {"metaObjName", ""},
        {"metaFieldRelationIDPath", ""},
        {"queryAreaSubNodes", false},
    };
}

QByteArray AttendanceClient::buildBody(const Config &config) const
{
    const QDate today = QDate::currentDate();
    const QString from = today.addDays(-config.daysBack).toString("yyyy/MM/dd");
    const QString to = today.toString("yyyy/MM/dd");
    const int capacity = config.daysBack > 10 ? 60 : 15;

    QJsonObject tableData{
        {"advance", QJsonObject{{"cmp_render", QJsonObject{{"viewPath", "MyAttendanceStatisticsTable"},
                                                            {"status", "enable"}}}}},
        {"hasCheckColumn", true},
        {"ext_data", QJsonObject{{"ListViewLabel", QStringLiteral("我的考勤列表")}}},
        {"columnGroups", QJsonValue::Null},
        {"isEnableGlobleCheck", false},
        {"hasRowHandler", true},
        {"paging", QJsonObject{{"total", 0}, {"capacity", capacity}, {"page", 0},
                               {"capacityList", QJsonArray{15, 30, 60, 100}}}},
        {"isAvatars", true},
        {"viewName", "Attendance.SingleObjectListView.EmpAttendanceDataList"},
        {"operateColumWidth", 140},
        {"extendsParam", ""},
        {"isSyncRowHandler", true},
        {"isFrozenOperationColumnHandler", false},
        {"isCustomListViewExisted", false},
        {"getTreeNodeUrl", QJsonValue::Null},
        {"sort_fields", QJsonArray{QJsonObject{{"sort_column", "SwipingCardDate"}, {"sort_dir", "desc"}}}},
        {"linkRuleJsonV2", QJsonValue::Null},
        {"description", QStringLiteral("员工出勤列表")},
        {"metaObjName", "Attendance.AttendanceStatistics"},
        {"isCustomListView", true},
        {"navViewIsCustom", false},
        {"navViewName", "Attendance.AttendanceDataRecordNavView"},
        {"navViewVersion", "20241029192525317"},
    };

    QJsonArray items{
        searchItem("StaffId", config.staffText, config.staffId, "1"),
        searchItem("AttendanceStatus", QStringLiteral("空 (未填写)"), "#AllowSearchNull#", "3"),
        searchItem("StdIsDeleted", QStringLiteral("否"), "0", "5"),
        searchItem("Status", QStringLiteral("启用"), "1", "6"),
        searchItem("SwipingCardDate", from + "~" + to, from + "-" + to, "2"),
    };

    QJsonObject searchData{
        {"metaObjName", "Attendance.AttendanceStatistics"},
        {"searchView", "Attendance.EmpAttendanceDataSearch"},
        {"items", items},
        {"searchFormFilterJson", QJsonValue::Null},
    };

    return QJsonDocument(QJsonObject{{"table_data", tableData}, {"search_data", searchData}})
        .toJson(QJsonDocument::Compact);
}

void AttendanceClient::fetch(const Config &config)
{
    if (m_busy)
        return;
    if (!config.isComplete()) {
        emit failed(QStringLiteral("请先在配置中填写 Cookie 和员工 ID"));
        return;
    }
    m_busy = true;

    QNetworkRequest req{QUrl(config.url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json;charset=UTF-8");
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Cookie", config.cookie.toUtf8());
    req.setRawHeader("Origin", "https://www.italent.cn");
    req.setRawHeader("Referer", "https://www.italent.cn/");
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply *reply = m_nam->post(req, buildBody(config));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_busy = false;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403 || (status >= 300 && status < 400)) {
            emit failed(QStringLiteral("登录已失效（HTTP %1），请更新 Cookie").arg(status));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("网络错误：%1").arg(reply->errorString()));
            return;
        }
        QString error;
        const auto records = parse(reply->readAll(), &error);
        if (!error.isEmpty())
            emit failed(error);
        else
            emit finished(records);
    });
}

static QString field(const QJsonObject &row, const char *name)
{
    const QJsonObject f = row.value(QLatin1String(name)).toObject();
    const QString text = f.value("text").toString().trimmed();
    return text.isEmpty() ? f.value("value").toString().trimmed() : text;
}

QList<AttendanceRecord> AttendanceClient::parse(const QByteArray &json, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("返回内容不是 JSON，Cookie 可能已失效");
        return {};
    }
    const QJsonObject root = doc.object();
    if (!root.contains("biz_data")) {
        const QString msg = root.value("message").toString(root.value("Message").toString());
        *error = msg.isEmpty() ? QStringLiteral("返回数据缺少 biz_data，Cookie 可能已失效") : msg;
        return {};
    }

    static const QRegularExpression timeRe(QStringLiteral("\\b(\\d{1,2}:\\d{2})"));
    QList<AttendanceRecord> out;
    for (const QJsonValue &v : root.value("biz_data").toArray()) {
        const QJsonObject row = v.toObject();
        AttendanceRecord r;
        r.reason = field(row, "AttendanceStatusReason");
        if (r.reason.isEmpty())
            continue; // normal day, nothing to report

        r.dateText = field(row, "SwipingCardDate");
        r.date = QDate::fromString(row.value("SwipingCardDate").toObject().value("value").toString(),
                                   "yyyy/MM/dd");
        r.firstCard = field(row, "ActualForFirstCard");
        r.lastCard = field(row, "ActualForLastCard");
        auto it = timeRe.globalMatch(r.reason);
        while (it.hasNext())
            r.missedTimes << it.next().captured(1);
        out << r;
    }
    return out;
}

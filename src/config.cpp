#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
const char *kDefaultUrl =
    "https://cloud.italent.cn/api/v2/UI/TableList"
    "?viewName=Attendance.SingleObjectListView.EmpAttendanceDataList"
    "&metaObjName=Attendance.AttendanceStatistics&app=Attendance"
    "&navViewName=Attendance.AttendanceDataRecordNavView"
    "&PaaS-SourceApp=Attendance&PaaS-CurrentView=Attendance.AttendanceDataRecordNavView"
    "&frontendVersion=2025121900&_qsrcapp=attendance";
}

QString Config::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

Config Config::load()
{
    Config c;
    c.url = QString::fromLatin1(kDefaultUrl);

    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        c.save();
        return c;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    c.url = o.value("url").toString(c.url);
    c.cookie = o.value("cookie").toString();
    c.staffId = o.value("staffId").toString();
    c.staffText = o.value("staffText").toString();
    c.intervalMinutes = qMax(1, o.value("intervalMinutes").toInt(c.intervalMinutes));
    c.daysBack = qBound(0, o.value("daysBack").toInt(c.daysBack), 31);
    c.autoPopup = o.value("autoPopup").toBool(c.autoPopup);
    const QJsonObject pos = o.value("panelPos").toObject();
    if (pos.contains("x") && pos.contains("y"))
        c.panelPos = QPoint(pos.value("x").toInt(), pos.value("y").toInt());
    return c;
}

bool Config::save() const
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonObject o{
        {"url", url},
        {"cookie", cookie},
        {"staffId", staffId},
        {"staffText", staffText},
        {"intervalMinutes", intervalMinutes},
        {"daysBack", daysBack},
        {"autoPopup", autoPopup},
    };
    if (panelPos)
        o.insert("panelPos", QJsonObject{{"x", panelPos->x()}, {"y", panelPos->y()}});
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return false;
    QFile::setPermissions(filePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

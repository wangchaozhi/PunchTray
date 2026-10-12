#include "trayapp.h"

#include "autostart.h"
#include "glasspanel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

namespace {
QIcon makeIcon(int badge, bool error)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size;

        // Glassy clock face.
        QRectF face(s * 0.06, s * 0.06, s * 0.88, s * 0.88);
        QLinearGradient g(face.topLeft(), face.bottomRight());
        g.setColorAt(0, QColor(120, 190, 255));
        g.setColorAt(1, QColor(40, 110, 230));
        p.setPen(QPen(QColor(255, 255, 255, 220), qMax(1.0, s / 20)));
        p.setBrush(g);
        p.drawEllipse(face);
        p.setPen(QPen(Qt::white, qMax(1.2, s / 11), Qt::SolidLine, Qt::RoundCap));
        const QPointF c = face.center();
        p.drawLine(c, c + QPointF(0, -s * 0.26));
        p.drawLine(c, c + QPointF(s * 0.18, s * 0.08));

        if (badge > 0 || error) {
            const qreal r = s * 0.24;
            const QPointF bc(s - r, r);
            p.setPen(Qt::NoPen);
            p.setBrush(error ? QColor(255, 159, 10) : QColor(255, 59, 48));
            p.drawEllipse(bc, r, r);
            if (size >= 24) {
                QFont f;
                f.setPixelSize(int(r * 1.4));
                f.setBold(true);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(QRectF(bc.x() - r, bc.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
                           error ? QStringLiteral("!") : QString::number(qMin(badge, 9)));
            }
        }
        icon.addPixmap(pm);
    }
    return icon;
}
} // namespace

TrayApp::TrayApp(QObject *parent)
    : QObject(parent)
    , m_config(Config::load())
    , m_client(new AttendanceClient(this))
    , m_panel(new GlassPanel)
    , m_tray(new QSystemTrayIcon(this))
    , m_timer(new QTimer(this))
{
    auto *menu = new QMenu;
    menu->addAction(QStringLiteral("显示 / 隐藏面板"), m_panel, &GlassPanel::toggle);
    menu->addAction(QStringLiteral("立即刷新"), this, &TrayApp::refresh);
    menu->addSeparator();
    menu->addAction(QStringLiteral("设置…"), this, &TrayApp::openSettings);
    menu->addAction(QStringLiteral("打开配置文件"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Config::filePath()));
    });
    QAction *autoStart = menu->addAction(QStringLiteral("开机自启动"));
    autoStart->setCheckable(true);
    autoStart->setChecked(AutoStart::isEnabled());
    connect(autoStart, &QAction::toggled, this, [this, autoStart](bool on) {
        if (!AutoStart::setEnabled(on)) {
            QSignalBlocker block(autoStart);
            autoStart->setChecked(!on);
            m_tray->showMessage(QStringLiteral("PunchTray"), QStringLiteral("设置开机自启动失败"),
                                QSystemTrayIcon::Warning);
        }
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("未打卡提醒"));
    updateIcon(0, false);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            m_panel->toggle();
    });
    m_tray->show();

    connect(m_client, &AttendanceClient::finished, this, &TrayApp::onRecords);
    connect(m_client, &AttendanceClient::failed, this, &TrayApp::onFailed);
    connect(m_panel, &GlassPanel::refreshRequested, this, &TrayApp::refresh);
    connect(m_timer, &QTimer::timeout, this, &TrayApp::refresh);

    m_panel->setIntervalMinutes(m_config.intervalMinutes);
    m_timer->start(m_config.intervalMinutes * 60 * 1000);

    if (!m_config.isComplete())
        QTimer::singleShot(0, this, &TrayApp::openSettings);
    else
        refresh();
}

TrayApp::~TrayApp()
{
    delete m_tray->contextMenu();
    delete m_panel;
}

void TrayApp::refresh()
{
    m_panel->setLoading(true);
    m_client->fetch(m_config);
}

void TrayApp::onRecords(const QList<AttendanceRecord> &records)
{
    m_panel->setLoading(false);
    m_panel->setRecords(records);
    updateIcon(records.size(), false);

    QStringList tip;
    bool fresh = false;
    for (const AttendanceRecord &r : records) {
        const QString key = r.date.toString(Qt::ISODate) + r.reason;
        tip << r.reason;
        if (!m_seen.contains(key)) {
            m_seen.insert(key);
            fresh = true;
        }
    }
    m_tray->setToolTip(records.isEmpty() ? QStringLiteral("打卡正常")
                                         : QStringLiteral("未打卡：") + tip.join(QStringLiteral("，")));
    if (fresh && m_config.autoPopup)
        m_panel->showAnimated();
}

void TrayApp::onFailed(const QString &message)
{
    m_panel->setLoading(false);
    m_panel->setError(message);
    updateIcon(0, true);
    m_tray->setToolTip(message);
}

void TrayApp::updateIcon(int missing, bool error)
{
    m_tray->setIcon(makeIcon(missing, error));
}

void TrayApp::openSettings()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("未打卡提醒 - 设置"));
    dlg.setMinimumWidth(520);

    auto *cookie = new QPlainTextEdit(m_config.cookie);
    cookie->setPlaceholderText(QStringLiteral("Tita_PC=...; ssn_Tita_PC=...; ..."));
    cookie->setFixedHeight(90);
    auto *staffId = new QLineEdit(m_config.staffId);
    staffId->setPlaceholderText(QStringLiteral("例如 641007593"));
    auto *staffText = new QLineEdit(m_config.staffText);
    staffText->setPlaceholderText(QStringLiteral("姓名(邮箱)"));
    auto *url = new QLineEdit(m_config.url);
    auto *interval = new QSpinBox;
    interval->setRange(1, 240);
    interval->setSuffix(QStringLiteral(" 分钟"));
    interval->setValue(m_config.intervalMinutes);
    auto *days = new QSpinBox;
    days->setRange(0, 31);
    days->setSuffix(QStringLiteral(" 天"));
    days->setSpecialValueText(QStringLiteral("仅今天"));
    days->setValue(m_config.daysBack);
    auto *popup = new QCheckBox(QStringLiteral("发现新的未打卡记录时自动弹出面板"));
    popup->setChecked(m_config.autoPopup);

    auto *form = new QFormLayout(&dlg);
    form->addRow(QStringLiteral("Cookie"), cookie);
    form->addRow(QStringLiteral("员工 ID"), staffId);
    form->addRow(QStringLiteral("员工名称"), staffText);
    form->addRow(QStringLiteral("接口地址"), url);
    form->addRow(QStringLiteral("刷新间隔"), interval);
    form->addRow(QStringLiteral("向前查询"), days);
    form->addRow(QString(), popup);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_config.cookie = cookie->toPlainText().simplified();
    m_config.staffId = staffId->text().trimmed();
    m_config.staffText = staffText->text().trimmed();
    m_config.url = url->text().trimmed();
    m_config.intervalMinutes = interval->value();
    m_config.daysBack = days->value();
    m_config.autoPopup = popup->isChecked();
    m_config.save();

    m_panel->setIntervalMinutes(m_config.intervalMinutes);
    m_timer->start(m_config.intervalMinutes * 60 * 1000);
    refresh();
}

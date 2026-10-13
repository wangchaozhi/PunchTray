#include "trayapp.h"

#include "autostart.h"
#include "glasspanel.h"
#include "updater.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
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
    , m_updater(new Updater(this))
    , m_updateTimer(new QTimer(this))
{
    auto *menu = new QMenu;
    menu->addAction(QStringLiteral("显示 / 隐藏面板"), m_panel, &GlassPanel::toggle);
    menu->addAction(QStringLiteral("立即刷新"), this, &TrayApp::refresh);
    menu->addAction(QStringLiteral("面板回到右上角"), m_panel, [this] {
        m_panel->resetPosition();
        m_panel->showAnimated();
    });
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
    setupUpdater(menu);
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

    connect(m_panel, &GlassPanel::positionChanged, this, [this](std::optional<QPoint> pos) {
        m_config.panelPos = pos;
        m_config.save();
    });
    m_panel->setCustomPosition(m_config.panelPos);
    m_panel->setIntervalMinutes(m_config.intervalMinutes);
    m_panel->setShowSeconds(m_config.showSeconds);
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

void TrayApp::setupUpdater(QMenu *menu)
{
    menu->addSeparator();
    m_updateAction = menu->addAction(QStringLiteral("检查更新"));
    QAction *version = menu->addAction(QStringLiteral("当前版本 v%1").arg(Updater::currentVersion()));
    version->setEnabled(false);

    m_updater->setToken(m_config.githubToken);
    connect(m_updateAction, &QAction::triggered, this, [this] {
        if (m_updater->hasUpdate())
            m_updater->install();
        else
            m_updater->check(true);
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (m_updater->hasUpdate())
            m_updater->install();
    });

    connect(m_updater, &Updater::updateAvailable, this, [this](const ReleaseInfo &r, bool manual) {
        m_updateAction->setText(QStringLiteral("更新到 %1").arg(r.tag));
        if (m_installWhenFound) {
            m_installWhenFound = false;
            m_updater->install();
            return;
        }
        if (!manual) {
            m_tray->showMessage(QStringLiteral("PunchTray 有新版本 %1").arg(r.tag),
                                QStringLiteral("点击这里，或在托盘菜单中选择“更新到 %1”").arg(r.tag));
            return;
        }
        const auto answer = QMessageBox::question(
            nullptr, QStringLiteral("PunchTray"),
            QStringLiteral("发现新版本 %1（当前 v%2），现在更新并重启吗？").arg(r.tag, Updater::currentVersion()));
        if (answer == QMessageBox::Yes)
            m_updater->install();
    });
    connect(m_updater, &Updater::upToDate, this, [this](bool manual) {
        m_installWhenFound = false;
        if (manual)
            m_tray->showMessage(QStringLiteral("PunchTray"),
                                QStringLiteral("已是最新版本 v%1").arg(Updater::currentVersion()));
    });
    connect(m_updater, &Updater::checkFailed, this, [this](const QString &message, bool manual) {
        m_installWhenFound = false;
        if (manual)
            m_tray->showMessage(QStringLiteral("PunchTray"), message, QSystemTrayIcon::Warning);
    });
    connect(m_updater, &Updater::downloadProgress, this, [this](int percent) {
        m_updateAction->setText(QStringLiteral("正在下载更新 %1%").arg(percent));
        m_tray->setToolTip(QStringLiteral("正在下载更新 %1%").arg(percent));
    });
    connect(m_updater, &Updater::installFailed, this, [this](const QString &message) {
        m_updateAction->setText(QStringLiteral("更新到 %1").arg(m_updater->latest().tag));
        m_tray->showMessage(QStringLiteral("PunchTray 更新失败"), message, QSystemTrayIcon::Warning);
    });

    // First check shortly after start-up, then every 6 hours.
    connect(m_updateTimer, &QTimer::timeout, this, [this] {
        if (m_config.autoUpdate && !m_updater->hasUpdate())
            m_updater->check(false);
    });
    m_updateTimer->start(6 * 60 * 60 * 1000);
    QTimer::singleShot(20 * 1000, this, [this] {
        if (m_config.autoUpdate)
            m_updater->check(false);
    });
}

void TrayApp::updateNow()
{
    if (m_updater->hasUpdate()) {
        m_updater->install();
        return;
    }
    m_installWhenFound = true;
    m_updater->check(true);
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
    staffId->setPlaceholderText(QStringLiteral("例如 123456789"));
    auto *staffText = new QLineEdit(m_config.staffText);
    staffText->setPlaceholderText(QStringLiteral("姓名(邮箱)"));
    auto *url = new QLineEdit(m_config.url);
    // Interval is stored in minutes; whole hours are shown in hours.
    auto *interval = new QSpinBox;
    auto *intervalUnit = new QComboBox;
    intervalUnit->addItem(QStringLiteral("分钟"), 1);
    intervalUnit->addItem(QStringLiteral("小时"), 60);
    const bool inHours = m_config.intervalMinutes % 60 == 0;
    intervalUnit->setCurrentIndex(inHours ? 1 : 0);
    interval->setRange(1, inHours ? 24 : 24 * 60);
    interval->setValue(inHours ? m_config.intervalMinutes / 60 : m_config.intervalMinutes);
    // Switching the unit keeps the same duration where possible.
    connect(intervalUnit, &QComboBox::currentIndexChanged, interval, [interval](int index) {
        if (index == 1) {
            const int hours = qBound(1, (interval->value() + 30) / 60, 24);
            interval->setRange(1, 24);
            interval->setValue(hours);
        } else {
            const int minutes = interval->value() * 60;
            interval->setRange(1, 24 * 60);
            interval->setValue(minutes);
        }
    });
    auto *intervalRow = new QHBoxLayout;
    intervalRow->addWidget(interval, 1);
    intervalRow->addWidget(intervalUnit);
    auto *days = new QSpinBox;
    days->setRange(0, 31);
    days->setSuffix(QStringLiteral(" 天"));
    days->setSpecialValueText(QStringLiteral("仅今天"));
    days->setValue(m_config.daysBack);
    auto *popup = new QCheckBox(QStringLiteral("发现新的未打卡记录时自动弹出面板"));
    popup->setChecked(m_config.autoPopup);
    auto *seconds = new QCheckBox(QStringLiteral("当前时间显示秒"));
    seconds->setChecked(m_config.showSeconds);
    auto *autoUpdate = new QCheckBox(QStringLiteral("自动检查更新"));
    autoUpdate->setChecked(m_config.autoUpdate);
    auto *token = new QLineEdit(m_config.githubToken);
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText(QStringLiteral("仅当 GitHub 仓库为私有时需要（只读 Contents 权限即可）"));

    auto *form = new QFormLayout(&dlg);
    form->addRow(QStringLiteral("Cookie"), cookie);
    form->addRow(QStringLiteral("员工 ID"), staffId);
    form->addRow(QStringLiteral("员工名称"), staffText);
    form->addRow(QStringLiteral("接口地址"), url);
    form->addRow(QStringLiteral("刷新间隔"), intervalRow);
    form->addRow(QStringLiteral("向前查询"), days);
    form->addRow(QString(), popup);
    form->addRow(QString(), seconds);
    form->addRow(QString(), autoUpdate);
    form->addRow(QStringLiteral("GitHub Token"), token);
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
    m_config.intervalMinutes = interval->value() * intervalUnit->currentData().toInt();
    m_config.daysBack = days->value();
    m_config.autoPopup = popup->isChecked();
    m_config.showSeconds = seconds->isChecked();
    m_panel->setShowSeconds(m_config.showSeconds);
    m_config.autoUpdate = autoUpdate->isChecked();
    m_config.githubToken = token->text().trimmed();
    m_updater->setToken(m_config.githubToken);
    m_config.save();

    m_panel->setIntervalMinutes(m_config.intervalMinutes);
    m_timer->start(m_config.intervalMinutes * 60 * 1000);
    refresh();
}

void TrayApp::showPanel()
{
    m_panel->showAnimated();
}

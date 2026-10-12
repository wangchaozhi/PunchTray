#include "glasspanel.h"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace {
constexpr int kShadow = 22;     // transparent margin used by the drop shadow
constexpr int kWidth = 320;     // card width
constexpr int kRadius = 24;
constexpr int kHeader = 62;
constexpr int kRow = 70;
constexpr int kFooter = 38;
constexpr int kScreenGap = 6;   // distance from the screen edge
constexpr int kBlurDown = 4;    // backdrop downscale factor
constexpr int kBlurPad = 24;    // extra captured border so the blur does not clamp

const QColor kRed(255, 69, 58);
const QColor kGreen(48, 209, 88);

void boxBlurPass(QImage &img, int radius, bool horizontal)
{
    const int w = img.width(), h = img.height();
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;
    QList<QRgb> line(inner);
    for (int o = 0; o < outer; ++o) {
        auto px = [&](int i) -> QRgb & {
            return horizontal ? reinterpret_cast<QRgb *>(img.scanLine(o))[i]
                              : reinterpret_cast<QRgb *>(img.scanLine(i))[o];
        };
        for (int i = 0; i < inner; ++i)
            line[i] = px(i);
        int r = 0, g = 0, b = 0;
        const int span = radius * 2 + 1;
        for (int k = -radius; k <= radius; ++k) {
            const QRgb c = line[qBound(0, k, inner - 1)];
            r += qRed(c); g += qGreen(c); b += qBlue(c);
        }
        for (int i = 0; i < inner; ++i) {
            px(i) = qRgb(r / span, g / span, b / span);
            const QRgb add = line[qMin(i + radius + 1, inner - 1)];
            const QRgb sub = line[qMax(i - radius, 0)];
            r += qRed(add) - qRed(sub);
            g += qGreen(add) - qGreen(sub);
            b += qBlue(add) - qBlue(sub);
        }
    }
}

// Blur + saturation boost; returns average luminance (0..255).
int frost(QImage &img)
{
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurPass(img, 5, true);
        boxBlurPass(img, 5, false);
    }
    qint64 lum = 0;
    for (int y = 0; y < img.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = row[x];
            const int l = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
            lum += l;
            auto sat = [l](int v) { return qBound(0, l + (v - l) * 14 / 10, 255); };
            row[x] = qRgb(sat(qRed(c)), sat(qGreen(c)), sat(qBlue(c)));
        }
    }
    const qint64 n = qint64(img.width()) * img.height();
    return n ? int(lum / n) : 0;
}

QString weekday(const QDate &d)
{
    static const char *names[] = {"一", "二", "三", "四", "五", "六", "日"};
    return QStringLiteral("星期") + QString::fromUtf8(names[d.dayOfWeek() - 1]);
}

QString shortTime(const QString &dateTime)
{
    // "2026/10/12 09:58:00" -> "09:58"
    const int space = dateTime.indexOf(' ');
    const QString t = space >= 0 ? dateTime.mid(space + 1) : dateTime;
    return t.left(5);
}
} // namespace

GlassPanel::GlassPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::NoDropShadowWindowHint)
    , m_backdropTimer(new QTimer(this))
    , m_tickTimer(new QTimer(this))
    , m_fade(new QPropertyAnimation(this, "windowOpacity", this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);

    m_fade->setDuration(180);
    m_fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (windowOpacity() < 0.01)
            hide();
    });

    // Only Windows can exclude this window from screen capture, so only there can the
    // backdrop be refreshed while the card is visible.
    m_backdropTimer->setInterval(700);
    connect(m_backdropTimer, &QTimer::timeout, this, [this] {
        captureBackdrop();
        update();
    });
    m_tickTimer->setInterval(30 * 1000);
    connect(m_tickTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    relayout();
}

void GlassPanel::setRecords(const QList<AttendanceRecord> &records)
{
    m_records = records;
    m_error.clear();
    m_hasData = true;
    m_updated = QDateTime::currentDateTime();
    relayout();
}

void GlassPanel::setError(const QString &message)
{
    m_error = message;
    m_updated = QDateTime::currentDateTime();
    relayout();
}

void GlassPanel::setLoading(bool loading)
{
    m_loading = loading;
    update();
}

void GlassPanel::relayout()
{
    int rows = qMax<int>(1, m_records.size());
    if (!m_error.isEmpty() && !m_records.isEmpty())
        ++rows;
    const int h = kHeader + rows * kRow + kFooter;
    const QSize size(kWidth + 2 * kShadow, h + 2 * kShadow);

    QScreen *s = QGuiApplication::primaryScreen();
    const QRect avail = s->availableGeometry();
    const QPoint topLeft(avail.right() - size.width() + kShadow - kScreenGap,
                         avail.top() - kShadow + kScreenGap);

    const bool changed = size != this->size() || pos() != topLeft;
    setFixedSize(size);
    move(topLeft);
    if (changed && isVisible())
        captureBackdrop();
    update();
}

QRect GlassPanel::cardRect() const
{
    return rect().adjusted(kShadow, kShadow, -kShadow, -kShadow);
}

QRect GlassPanel::closeButtonRect() const
{
    const QRect c = cardRect();
    return QRect(c.right() - 16 - 26, c.top() + 18, 26, 26);
}

QRect GlassPanel::refreshButtonRect() const
{
    return closeButtonRect().translated(-34, 0);
}

void GlassPanel::captureBackdrop()
{
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect global = cardRect().translated(pos()).adjusted(-kBlurPad, -kBlurPad, kBlurPad, kBlurPad);
    const QRect local = global.translated(-s->geometry().topLeft());
    const QPixmap shot = s->grabWindow(0, local.x(), local.y(), local.width(), local.height());
    if (shot.isNull() || shot.width() < 8) {
        m_backdrop = QImage();
        return;
    }
    QImage frosted = shot.toImage()
                       .scaled(shot.width() / kBlurDown, shot.height() / kBlurDown,
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    const int lum = frost(frosted);
    m_darkBackdrop = lum < 150;
    m_backdrop = frosted;
}

void GlassPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
#ifdef Q_OS_WIN
    // Hidden from screen capture so the live backdrop never samples the card itself.
    // PUNCHTRAY_CAPTURABLE=1 turns this off (for screenshots) and freezes the backdrop.
    if (!qEnvironmentVariableIsSet("PUNCHTRAY_CAPTURABLE"))
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE);
#endif
}

void GlassPanel::showAnimated()
{
    relayout();
    if (!isVisible()) {
        captureBackdrop(); // still hidden, so the capture is clean on every platform
        setWindowOpacity(0.0);
        show();
    }
    raise();
#ifdef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("PUNCHTRAY_CAPTURABLE"))
        m_backdropTimer->start();
#endif
    m_tickTimer->start();
    m_fade->stop();
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(1.0);
    m_fade->start();
}

void GlassPanel::hideAnimated()
{
    m_backdropTimer->stop();
    m_tickTimer->stop();
    m_fade->stop();
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void GlassPanel::toggle()
{
    if (isVisible() && windowOpacity() > 0.5)
        hideAnimated();
    else
        showAnimated();
}

void GlassPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    const QRectF card = cardRect();
    QPainterPath shape;
    shape.addRoundedRect(card, kRadius, kRadius);

    // Soft layered drop shadow.
    for (int i = kShadow; i > 0; i -= 2) {
        QPainterPath sp;
        sp.addRoundedRect(card.adjusted(-i, -i + 6, i, i + 6), kRadius + i, kRadius + i);
        p.fillPath(sp, QColor(0, 0, 0, int(5.0 * (1.0 - double(i) / kShadow) + 1)));
    }

    paintBackdrop(p, shape);
    paintContent(p);
}

void GlassPanel::paintBackdrop(QPainter &p, const QPainterPath &shape)
{
    const QRectF card = cardRect();
    p.save();
    p.setClipPath(shape);

    if (!m_backdrop.isNull()) {
        // Slight magnification of the backdrop reads as refraction through a thick lens.
        const QRectF captured = card.adjusted(-kBlurPad, -kBlurPad, kBlurPad, kBlurPad);
        const QPointF c = card.center();
        const qreal k = 1.035;
        const QRectF lens(c.x() - captured.width() * k / 2, c.y() - captured.height() * k / 2,
                          captured.width() * k, captured.height() * k);
        p.drawImage(lens, m_backdrop);
        p.fillPath(shape, m_darkBackdrop ? QColor(20, 20, 28, 70) : QColor(255, 255, 255, 90));
    } else {
        QLinearGradient g(card.topLeft(), card.bottomRight());
        g.setColorAt(0, QColor(60, 66, 88, 215));
        g.setColorAt(1, QColor(28, 30, 42, 225));
        p.fillPath(shape, g);
        m_darkBackdrop = true;
    }

    // Edge refraction: light pooled along the rim, dark in the middle.
    QRadialGradient rim(card.center(), qMax(card.width(), card.height()) * 0.75);
    rim.setColorAt(0.55, QColor(255, 255, 255, 0));
    rim.setColorAt(1.0, QColor(255, 255, 255, 46));
    p.fillPath(shape, rim);

    // Specular sheen across the top.
    QLinearGradient sheen(card.topLeft(), QPointF(card.left(), card.top() + card.height() * 0.45));
    sheen.setColorAt(0, QColor(255, 255, 255, 70));
    sheen.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillPath(shape, sheen);

    // Glossy highlight blob in the top-left corner.
    QRadialGradient blob(card.topLeft() + QPointF(40, 8), 120);
    blob.setColorAt(0, QColor(255, 255, 255, 55));
    blob.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillPath(shape, blob);
    p.restore();

    // Bevel: bright top-left edge fading to a dimmer bottom-right edge.
    QLinearGradient edge(card.topLeft(), card.bottomRight());
    edge.setColorAt(0.0, QColor(255, 255, 255, 230));
    edge.setColorAt(0.35, QColor(255, 255, 255, 50));
    edge.setColorAt(0.7, QColor(255, 255, 255, 30));
    edge.setColorAt(1.0, QColor(255, 255, 255, 150));
    p.setPen(QPen(QBrush(edge), 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(0.65, 0.65, -0.65, -0.65), kRadius, kRadius);
    p.setPen(QPen(QColor(0, 0, 0, m_darkBackdrop ? 60 : 25), 1));
    p.drawRoundedRect(card.adjusted(-0.5, -0.5, 0.5, 0.5), kRadius + 0.5, kRadius + 0.5);
}

void GlassPanel::paintContent(QPainter &p)
{
    const QRect card = cardRect();
    const QColor fg = m_darkBackdrop ? QColor(255, 255, 255) : QColor(22, 22, 26);
    QColor dim = fg;
    dim.setAlpha(165);
    QColor faint = fg;
    faint.setAlpha(28);

    QFont base = font();
    base.setFamilies({"Segoe UI Variable Display", "Segoe UI", "PingFang SC", "Microsoft YaHei",
                      "Noto Sans CJK SC", "sans-serif"});
    auto fontOf = [&](qreal px, int weight) {
        QFont f = base;
        f.setPixelSize(int(px));
        f.setWeight(QFont::Weight(weight));
        return f;
    };

    // Header -----------------------------------------------------------------
    const int x = card.left() + 20;
    const QDate today = QDate::currentDate();
    const bool alarm = !m_records.isEmpty();

    p.setPen(Qt::NoPen);
    p.setBrush(alarm ? kRed : (m_hasData && m_error.isEmpty() ? kGreen : QColor(255, 159, 10)));
    p.drawEllipse(QPointF(x + 4, card.top() + 27), 4, 4);

    p.setPen(fg);
    p.setFont(fontOf(15, QFont::DemiBold));
    p.drawText(QRect(x + 16, card.top() + 16, 200, 22), Qt::AlignLeft | Qt::AlignVCenter,
               alarm ? QStringLiteral("未打卡提醒") : QStringLiteral("考勤状态"));
    p.setPen(dim);
    p.setFont(fontOf(12, QFont::Normal));
    p.drawText(QRect(x + 16, card.top() + 37, 200, 16), Qt::AlignLeft | Qt::AlignVCenter,
               today.toString(QStringLiteral("M月d日 ")) + weekday(today));

    auto drawButton = [&](const QRect &r, bool hover, const QString &glyph) {
        p.setPen(Qt::NoPen);
        QColor bg = fg;
        bg.setAlpha(hover ? 46 : 20);
        p.setBrush(bg);
        p.drawEllipse(r);
        p.setPen(fg);
        p.setFont(fontOf(13, QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, glyph);
    };
    drawButton(refreshButtonRect(), m_hover == 2, m_loading ? QStringLiteral("…") : QStringLiteral("↻"));
    drawButton(closeButtonRect(), m_hover == 1, QStringLiteral("✕"));

    // Rows -------------------------------------------------------------------
    int y = card.top() + kHeader;
    auto separator = [&](int yy) {
        p.setPen(QPen(faint, 1));
        p.drawLine(card.left() + 20, yy, card.right() - 20, yy);
    };

    const QDateTime now = QDateTime::currentDateTime();
    for (const AttendanceRecord &r : m_records) {
        separator(y);
        const QString time = r.missedTimes.isEmpty() ? QStringLiteral("--:--") : r.missedTimes.first();

        p.setPen(fg);
        p.setFont(fontOf(30, QFont::DemiBold));
        const QRect timeRect(x, y + 8, 120, 36);
        p.drawText(timeRect, Qt::AlignLeft | Qt::AlignVCenter, time);
        const int timeWidth = p.fontMetrics().horizontalAdvance(time);

        // "未打卡" pill
        QString label = r.reason;
        for (const QString &t : r.missedTimes)
            label.remove(t);
        label = label.trimmed();
        if (label.isEmpty())
            label = QStringLiteral("未打卡");
        p.setFont(fontOf(11, QFont::DemiBold));
        const int pillW = p.fontMetrics().horizontalAdvance(label) + 16;
        const QRectF pill(x + timeWidth + 10, y + 17, pillW, 20);
        p.setPen(Qt::NoPen);
        QColor pillBg = kRed;
        pillBg.setAlpha(m_darkBackdrop ? 200 : 230);
        p.setBrush(pillBg);
        p.drawRoundedRect(pill, 10, 10);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, label);

        // Countdown for today's records.
        const QTime missed = QTime::fromString(time, "H:mm");
        if (r.date == today && missed.isValid()) {
            const qint64 mins = now.secsTo(QDateTime(today, missed)) / 60;
            QString text;
            QColor color = dim;
            if (mins > 0)
                text = mins >= 60 ? QStringLiteral("还有 %1 小时 %2 分").arg(mins / 60).arg(mins % 60)
                                  : QStringLiteral("还有 %1 分钟").arg(mins);
            else {
                text = QStringLiteral("已过 %1 分钟").arg(-mins);
                color = kRed;
            }
            p.setPen(color);
            p.setFont(fontOf(12, QFont::DemiBold));
            p.drawText(QRect(card.right() - 20 - 120, y + 17, 120, 20), Qt::AlignRight | Qt::AlignVCenter, text);
        }

        p.setPen(dim);
        p.setFont(fontOf(12, QFont::Normal));
        QString detail = r.date.isValid() ? r.date.toString("M/d ") + weekday(r.date) : r.dateText;
        if (!r.firstCard.isEmpty())
            detail += QStringLiteral("  ·  上班 ") + shortTime(r.firstCard);
        if (!r.lastCard.isEmpty())
            detail += QStringLiteral("  ·  下班 ") + shortTime(r.lastCard);
        p.drawText(QRect(x, y + 46, card.width() - 40, 16), Qt::AlignLeft | Qt::AlignVCenter, detail);
        y += kRow;
    }

    if (m_records.isEmpty() || !m_error.isEmpty()) {
        separator(y);
        QString title, sub;
        QColor accent = fg;
        if (!m_error.isEmpty()) {
            title = QStringLiteral("查询失败");
            sub = m_error;
            accent = QColor(255, 159, 10);
        } else if (m_hasData) {
            title = QStringLiteral("打卡正常");
            sub = QStringLiteral("没有未打卡记录");
            accent = kGreen;
        } else {
            title = QStringLiteral("正在查询…");
            sub = QStringLiteral("首次加载中");
        }
        p.setPen(accent);
        p.setFont(fontOf(18, QFont::DemiBold));
        p.drawText(QRect(x, y + 12, card.width() - 40, 26), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setPen(dim);
        p.setFont(fontOf(12, QFont::Normal));
        p.drawText(QRect(x, y + 40, card.width() - 40, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(sub, Qt::ElideRight, card.width() - 40));
        y += kRow;
    }

    // Footer -----------------------------------------------------------------
    separator(y);
    p.setPen(dim);
    p.setFont(fontOf(11, QFont::Normal));
    const QString footer = m_updated.isValid()
        ? QStringLiteral("更新于 %1 · 每 %2 分钟刷新").arg(m_updated.toString("HH:mm")).arg(m_interval)
        : QStringLiteral("每 %1 分钟刷新").arg(m_interval);
    p.drawText(QRect(x, y, card.width() - 40, kFooter), Qt::AlignLeft | Qt::AlignVCenter, footer);
}

void GlassPanel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (closeButtonRect().contains(e->position().toPoint()))
        hideAnimated();
    else if (refreshButtonRect().contains(e->position().toPoint()))
        emit refreshRequested();
}

void GlassPanel::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pt = e->position().toPoint();
    const int hover = closeButtonRect().contains(pt) ? 1 : refreshButtonRect().contains(pt) ? 2 : 0;
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void GlassPanel::leaveEvent(QEvent *)
{
    if (m_hover) {
        m_hover = 0;
        update();
    }
}

#pragma once

#include "attendanceclient.h"

#include <QDateTime>
#include <QImage>
#include <QWidget>

#include <optional>

class QPropertyAnimation;
class QVariantAnimation;
class QTimer;

// Frameless always-on-top "liquid glass" card, by default in the top-right corner of the
// primary screen. Everything is custom painted; the frosted backdrop is a blurred
// capture of the desktop behind the card. It can be dragged anywhere.
class GlassPanel : public QWidget
{
    Q_OBJECT
public:
    explicit GlassPanel(QWidget *parent = nullptr);

    void setRecords(const QList<AttendanceRecord> &records);
    void setError(const QString &message);
    void setLoading(bool loading);
    void setIntervalMinutes(int minutes) { m_interval = minutes; update(); }

    void showAnimated();
    void hideAnimated();
    void toggle();

    // Card top-left in global coordinates; nullopt pins it to the top-right corner.
    void setCustomPosition(std::optional<QPoint> cardTopLeft);
    void resetPosition();

signals:
    void refreshRequested();
    void positionChanged(std::optional<QPoint> cardTopLeft);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void showEvent(QShowEvent *e) override;

private:
    void relayout();
    QRect cardRect() const;
    QRect closeButtonRect() const;
    QRect refreshButtonRect() const;
    void captureBackdrop();
    void captureBackdropBehind();
    void paintBackdrop(QPainter &p, const QPainterPath &shape);
    void paintContent(QPainter &p);
    void paintClock(QPainter &p, const QRect &area, const QColor &fg, const QColor &faint);
    void tick();
    void scheduleTick();

    QList<AttendanceRecord> m_records;
    QString m_error;
    bool m_loading = false;
    bool m_hasData = false;
    QDateTime m_updated;
    int m_interval = 10;

    QImage m_backdrop;          // blurred, card-sized, device-pixel scaled
    bool m_darkBackdrop = true; // decides text colour
    QTimer *m_backdropTimer;
    QTimer *m_tickTimer;        // fires on every wall-clock second while visible
    QVariantAnimation *m_roll;  // 0..1 progress of the digit roll after a tick
    QString m_clockPrev;        // "HH:mm:ss" before the last tick
    QString m_clockNow;
    int m_secondPrev = 0;
    int m_secondNow = 0;
    QPropertyAnimation *m_fade;
    int m_hover = 0;            // 0 none, 1 close, 2 refresh

    std::optional<QPoint> m_customPos; // dragged card position (global, card top-left)
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_dragOffset;
};

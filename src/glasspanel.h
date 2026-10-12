#pragma once

#include "attendanceclient.h"

#include <QDateTime>
#include <QImage>
#include <QWidget>

class QPropertyAnimation;
class QTimer;

// Frameless always-on-top "liquid glass" card pinned to the top-right corner of the
// primary screen. Everything is custom painted; the frosted backdrop is a blurred
// capture of the desktop behind the card.
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

signals:
    void refreshRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void showEvent(QShowEvent *e) override;

private:
    void relayout();
    QRect cardRect() const;
    QRect closeButtonRect() const;
    QRect refreshButtonRect() const;
    void captureBackdrop();
    void paintBackdrop(QPainter &p, const QPainterPath &shape);
    void paintContent(QPainter &p);

    QList<AttendanceRecord> m_records;
    QString m_error;
    bool m_loading = false;
    bool m_hasData = false;
    QDateTime m_updated;
    int m_interval = 10;

    QImage m_backdrop;          // blurred, card-sized, device-pixel scaled
    bool m_darkBackdrop = true; // decides text colour
    QTimer *m_backdropTimer;
    QTimer *m_tickTimer;        // keeps countdowns fresh
    QPropertyAnimation *m_fade;
    int m_hover = 0;            // 0 none, 1 close, 2 refresh
};

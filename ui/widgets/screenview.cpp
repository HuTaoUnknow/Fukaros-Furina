#include "screenview.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

ScreenView::ScreenView(QWidget* p) : QWidget(p) {
    setMinimumSize(160, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    QPalette colors = palette();
    colors.setColor(QPalette::Window, Qt::black);
    setPalette(colors);
}

void ScreenView::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    m_pressed = false;
    if (busy)
        m_image = {};
    update();
}

void ScreenView::setImage(const QImage& im) {
    if (m_busy && !im.isNull())
        return;
    m_image = im;
    ++m_revision;
    update();
}

void ScreenView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_image.isNull()) {
        p.setPen(QColor(134, 148, 163));
        p.drawText(rect(), Qt::AlignCenter,
                   m_busy ? QStringLiteral("占用中...\nScreen sharing in progress")
                          : QStringLiteral("等待投屏\nWaiting for screen stream"));
        return;
    }
    auto size = m_image.size().scaled(this->size(), Qt::KeepAspectRatio);
    QRect dest(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.drawImage(dest, m_image);
    if (m_painted != m_revision) {
        m_painted = m_revision;
        ++m_displayed;
    }
}

void ScreenView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
        emit leaveFullScreen();
        event->accept();
    } else
        QWidget::keyPressEvent(event);
}

void ScreenView::mousePressEvent(QMouseEvent* e) {
    m_pressed = false;
    if (m_busy || !m_interactive || e->button() != Qt::LeftButton)
        return;
    m_pressed = fr_map_point(e->position().x(), e->position().y(), width(), height(), m_image.width(),
                             m_image.height(), &m_x, &m_y);
    if (m_pressed)
        m_press.start();
}

void ScreenView::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_pressed || e->button() != Qt::LeftButton)
        return;
    m_pressed = false;
    uint32_t x, y;
    if (!fr_map_point(e->position().x(), e->position().y(), width(), height(), m_image.width(),
                      m_image.height(), &x, &y))
        return;
    fr_gesture g{m_x, m_y, x, y, uint32_t(qBound(qint64(40), m_press.elapsed(), qint64(2000)))};
    emit gesture(g);
}

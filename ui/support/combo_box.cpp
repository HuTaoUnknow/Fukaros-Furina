#include "interface_support.h"
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>

void FurinaComboBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isEnabled() ? QColor("#d9e5ee") : QColor("#687a8b"));
    const qreal x = width() - 13, y = height() / 2.0;
    painter.drawPolygon(QPolygonF{QPointF(x - 4, y - 2), QPointF(x + 4, y - 2), QPointF(x, y + 3)});
}

void FurinaComboBox::showPopup() {
#ifdef Q_OS_ANDROID
    // Keep the popup in the current surface: secondary native widget windows can deadlock on Android.
    if (!m_popup) {
        m_popup = new QListWidget(window());
        m_popup->setObjectName("comboOptions");
        m_popup->setStyleSheet(
            "QListWidget{background:#253344;border:1px solid #7497b4;} QListWidget::item{padding:12px;min-height:20px;} QListWidget::item:selected{background:#216e89;}");
        connect(m_popup, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const auto index = m_popup->row(item);
            setCurrentIndex(index);
            hidePopup();
            emit activated(index);
        });
    }
    m_popup->clear();
    for (int i = 0; i < count(); ++i)
        m_popup->addItem(itemText(i));
    const int popupHeight = qMin(qMax(44, count() * 44 + 4), qMax(44, window()->height() - 32));
    const int popupWidth = qMin(qMax(width(), 160), window()->width() - 24);
    QPoint pos = mapTo(window(), QPoint(0, height()));
    if (pos.y() + popupHeight > window()->height() - 8)
        pos.setY(qMax(8, mapTo(window(), QPoint()).y() - popupHeight));
    pos.setX(qBound(8, pos.x(), qMax(8, window()->width() - popupWidth - 8)));
    m_popup->setGeometry(pos.x(), pos.y(), popupWidth, popupHeight);
    m_popup->setCurrentRow(currentIndex());
    m_popup->show();
    m_popup->raise();
    m_popup->setFocus();
    qApp->installEventFilter(this);
#else
    QComboBox::showPopup();
#endif
}

void FurinaComboBox::hidePopup() {
#ifdef Q_OS_ANDROID
    if (m_popup)
        m_popup->hide();
    qApp->removeEventFilter(this);
    setFocus();
#else
    QComboBox::hidePopup();
#endif
}

bool FurinaComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (m_popup && m_popup->isVisible()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!m_popup->rect().contains(m_popup->mapFromGlobal(mouse->globalPosition().toPoint())) &&
                watched != this)
                hidePopup();
        } else if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Back || key->key() == Qt::Key_Escape) {
                hidePopup();
                return true;
            }
        }
    }
    return QComboBox::eventFilter(watched, event);
}

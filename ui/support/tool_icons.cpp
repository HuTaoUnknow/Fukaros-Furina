#include "interface_support.h"
#include <QPainter>
#include <QIconEngine>
#include <QPainterPath>
#include <QEvent>
#include <cmath>

namespace {
class ToolIconEngine final : public QIconEngine {
  public:
    enum Kind { Gear, Maximize, Restore, WindowMinimize, WindowMaximize, WindowRestore, WindowClose };

    explicit ToolIconEngine(Kind kind) : m_kind(kind) {}

    QIconEngine* clone() const override {
        return new ToolIconEngine(m_kind);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal scale = qMin(rect.width(), rect.height()) / 24.0;
        painter->translate(rect.center().x() + 0.5 - 12 * scale, rect.center().y() + 0.5 - 12 * scale);
        painter->scale(scale, scale);
        const QColor color = mode == QIcon::Disabled ? QColor("#768393") : QColor("#e4eaf0");
        if (m_kind == Gear) {
            QPainterPath gear;
            gear.setFillRule(Qt::OddEvenFill);
            constexpr double pi = 3.141592653589793;
            const double offsets[] = {-22.5, -14, -11, 11, 14, 22.5};
            for (int tooth = 0; tooth < 8; ++tooth)
                for (int step = 0; step < 6; ++step) {
                    const double angle = (tooth * 45 + offsets[step]) * pi / 180.0,
                                 radius = (step == 2 || step == 3) ? 10 : 8;
                    const QPointF point(12 + radius * std::cos(angle), 12 + radius * std::sin(angle));
                    if (tooth == 0 && step == 0)
                        gear.moveTo(point);
                    else
                        gear.lineTo(point);
                }
            gear.closeSubpath();
            gear.addEllipse(QPointF(12, 12), 3.2, 3.2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPath(gear);
        } else if (m_kind >= WindowMinimize) {
            painter->setPen(QPen(color, 1.6, Qt::SolidLine, Qt::SquareCap));
            painter->setBrush(Qt::NoBrush);
            if (m_kind == WindowMinimize)
                painter->drawLine(QPointF(5, 16), QPointF(19, 16));
            else if (m_kind == WindowClose) {
                painter->drawLine(QPointF(6, 6), QPointF(18, 18));
                painter->drawLine(QPointF(18, 6), QPointF(6, 18));
            } else if (m_kind == WindowMaximize)
                painter->drawRect(QRectF(5, 5, 14, 14));
            else {
                painter->drawPolyline(QPolygonF{QPointF(9, 8), QPointF(9, 5), QPointF(19, 5), QPointF(19, 15),
                                                QPointF(16, 15)});
                painter->drawRect(QRectF(5, 9, 10, 10));
            }
        } else {
            painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            for (int corner = 0; corner < 4; ++corner) {
                painter->save();
                painter->translate(12, 12);
                painter->rotate(corner * 90);
                painter->translate(-12, -12);
                QPainterPath arrow;
                if (m_kind == Maximize) {
                    arrow.moveTo(4, 9);
                    arrow.lineTo(4, 4);
                    arrow.lineTo(9, 4);
                    arrow.moveTo(4, 4);
                    arrow.lineTo(9, 9);
                } else {
                    arrow.moveTo(4, 9);
                    arrow.lineTo(9, 9);
                    arrow.lineTo(9, 4);
                    arrow.moveTo(9, 9);
                    arrow.lineTo(4, 4);
                }
                painter->drawPath(arrow);
                painter->restore();
            }
        }
        painter->restore();
    }

  private:
    Kind m_kind;
};
} // namespace

void furinaSetMaximizeIcon(QPushButton* button, bool restore) {
    button->setText({});
    button->setToolTip(restore ? "还原" : "放大");
    button->setAccessibleName(button->toolTip());
    button->setIcon(QIcon(new ToolIconEngine(restore ? ToolIconEngine::Restore : ToolIconEngine::Maximize)));
    button->setIconSize(QSize(22, 22));
}

void furinaConfigureToolIcons(QPushButton* settings, QPushButton* maximize) {
    settings->setText({});
    settings->setToolTip("设置");
    settings->setAccessibleName("设置");
    settings->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::Gear)));
    settings->setIconSize(QSize(22, 22));
    furinaSetMaximizeIcon(maximize, false);
}

WindowControls::WindowControls(QMainWindow* window, Presentation* presentation, ScreenView* view,
                               QWidget* controls)
    : QObject(window), m_window(window), m_presentation(presentation), m_view(view), m_controls(controls) {
    controls->setParent(view);
    auto* minimize = controls->findChild<QPushButton*>("windowMinimize");
    auto* maximize = controls->findChild<QPushButton*>("windowMaximize");
    auto* close = controls->findChild<QPushButton*>("windowClose");
    minimize->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::WindowMinimize)));
    close->setIcon(QIcon(new ToolIconEngine(ToolIconEngine::WindowClose)));
    for (auto* button : {minimize, maximize, close}) {
        button->setText({});
        button->setIconSize(QSize(18, 18));
    }
    connect(minimize, &QPushButton::clicked, window, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, this, [this] {
        if (m_presentation->active()) {
            m_presentation->setActive(false);
            m_window->showMaximized();
        } else if (m_window->isMaximized())
            m_window->showNormal();
        else
            m_window->showMaximized();
    });
    connect(close, &QPushButton::clicked, window, &QWidget::close);
    connect(presentation, &Presentation::changed, this, [this] {
        refresh();
        arrange();
    });
    view->installEventFilter(this);
    window->installEventFilter(this);
    controls->show();
    refresh();
    arrange();
}

void WindowControls::refresh() {
    const bool restore = m_window->isMaximized() || m_presentation->active();
    auto* button = m_controls->findChild<QPushButton*>("windowMaximize");
    button->setIcon(
        QIcon(new ToolIconEngine(restore ? ToolIconEngine::WindowRestore : ToolIconEngine::WindowMaximize)));
    button->setToolTip(restore ? "还原窗口" : "最大化窗口");
    button->setAccessibleName(button->toolTip());
}

void WindowControls::arrange() {
    m_controls->adjustSize();
    m_controls->move(qMax(0, m_view->width() - m_controls->width() - 12), 12);
    m_controls->raise();
    auto* exit = m_view->findChild<QPushButton*>("exitFullScreen");
    if (exit) {
        exit->adjustSize();
        exit->move(qMax(4, m_controls->x() - exit->width() - 12), 12);
    }
}

bool WindowControls::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_view && event->type() == QEvent::Resize)
        arrange();
    if (watched == m_window && event->type() == QEvent::WindowStateChange) {
        refresh();
        arrange();
    }
    return QObject::eventFilter(watched, event);
}

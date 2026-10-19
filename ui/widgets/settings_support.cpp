#include "settings_support.h"
#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QHideEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QTabWidget>
#include <QTabBar>
#include <QDialog>

namespace {
QWidget* helpHost(QWidget* button) {
    // Android embeds the settings dialog as a child of the activity window.
    // Keep its help on that same dialog, including its clipping and lifetime.
    for (auto* parent = button->parentWidget(); parent; parent = parent->parentWidget())
        if (qobject_cast<QDialog*>(parent))
            return parent;
    return button->window();
}

// QTabWidget can keep its old preferred height when a styled page grows.
// Give the entire current page its minimum height so the outer scroll area,
// rather than the tab itself, owns all vertical scrolling.
class SettingsPageSizing : public QObject {
  public:
    explicit SettingsPageSizing(QScrollArea* scroll) : QObject(scroll), m_scroll(scroll) {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { fit(); });
        scroll->widget()->installEventFilter(this);
        scroll->viewport()->installEventFilter(this);
        for (auto* tab : scroll->widget()->findChildren<QTabWidget*>()) {
            tab->installEventFilter(this);
            connect(tab, &QTabWidget::currentChanged, this, [this] {
                fit();
                m_timer.start(0);
            });
        }
        m_timer.start(0);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
            event->type() == QEvent::LayoutRequest || event->type() == QEvent::StyleChange ||
            event->type() == QEvent::FontChange)
            m_timer.start(0);
        return QObject::eventFilter(watched, event);
    }

  private:
    QScrollArea* m_scroll;
    QTimer m_timer;

    void fit() {
        for (auto* tab : m_scroll->widget()->findChildren<QTabWidget*>()) {
            auto* page = tab->currentWidget();
            if (!page || !page->layout())
                continue;
            for (auto* widget : page->findChildren<QWidget*>())
                widget->ensurePolished();
            page->layout()->activate();
            const int height =
                page->layout()->totalMinimumSize().height() + tab->tabBar()->sizeHint().height() + 20;
            if (tab->minimumHeight() != height || tab->maximumHeight() != height)
                tab->setFixedHeight(height);
        }
    }
};
} // namespace

StepHelpButton::StepHelpButton(QWidget* parent) : QPushButton("…", parent) {
    setProperty("stepHelp", true);
    setFixedSize(32, 16);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName("查看操作步骤");
    setAccessibleDescription("点击显示操作步骤，再次点击关闭");
    connect(this, &QPushButton::clicked, this, [this] {
        if (m_popup && m_popup->isVisible())
            hideHelp();
        else
            showHelp();
    });
    qApp->installEventFilter(this);
}

void StepHelpButton::setHelpTitle(const QString& title) {
    m_title = title;
    setAccessibleName(title + "操作步骤");
}

void StepHelpButton::setSteps(const QString& steps) {
    m_steps = steps;
}

void StepHelpButton::showHelp() {
    if (!isEnabled() || m_steps.isEmpty())
        return;
    // Desktop clicks and touch taps use the same toggle. Hover never opens it.
    auto* host = helpHost(this);
    for (auto* button : host->findChildren<StepHelpButton*>())
        if (button != this)
            button->hideHelp();
    if (!m_popup) {
        m_popup = new QFrame(host);
        m_popup->setObjectName("stepsPopup");
        m_popup->setProperty("stepPopup", true);
        m_popup->setAttribute(Qt::WA_StyledBackground);
        m_popup->setFrameShape(QFrame::NoFrame);
        auto* layout = new QVBoxLayout(m_popup);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(10);
        auto* heading = new QHBoxLayout;
        auto* title = new QLabel(m_title, m_popup);
        title->setObjectName("stepsTitle");
        title->setWordWrap(true);
        heading->addWidget(title, 1);
        auto* close = new QPushButton("×", m_popup);
        close->setObjectName("closeSteps");
        close->setAccessibleName("关闭操作步骤");
        close->setFixedSize(32, 32);
        heading->addWidget(close);
        layout->addLayout(heading);
        auto* body = new QScrollArea(m_popup);
        body->setObjectName("stepsScroll");
        body->setWidgetResizable(true);
        body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* text = new QLabel(m_steps, body);
        text->setObjectName("stepsBody");
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        body->setWidget(text);
        layout->addWidget(body, 1);
        furinaConfigureSettingsScroll(body);
        connect(close, &QPushButton::clicked, this, &StepHelpButton::hideHelp);
    }
    const int width = qMax(120, qMin(440, host->width() - 24));
    m_popup->setFixedWidth(width);
    auto* text = m_popup->findChild<QLabel*>("stepsBody");
    text->setText(m_steps);
    m_popup->findChild<QLabel*>("stepsTitle")->setText(m_title);
    const int textHeight = text->heightForWidth(qMax(60, width - 42));
    const auto anchor = mapTo(host, QPoint());
    const int below = qMax(0, host->height() - 12 - anchor.y() - height() - 6);
    const int above = qMax(0, anchor.y() - 18);
    const int desiredHeight = qMax(100, qMin(qMax(100, host->height() - 24), textHeight + 82));
    const bool useBelow = desiredHeight <= below || below >= above;
    // Never cover the trigger: touch release must still reach its button.
    // Long instructions use the body's scroll area when neither side fits.
    m_popup->setFixedHeight(qMin(desiredHeight, qMax(1, useBelow ? below : above)));
    int x = anchor.x() + this->width() - width;
    int y = useBelow ? anchor.y() + height() + 6 : anchor.y() - m_popup->height() - 6;
    x = qBound(12, x, qMax(12, host->width() - width - 12));
    y = qBound(12, y, qMax(12, host->height() - m_popup->height() - 12));
    m_popup->move(x, y);
    m_popup->show();
    m_popup->raise();
}

void StepHelpButton::hideHelp() {
    if (m_popup)
        m_popup->hide();
}

void StepHelpButton::hideEvent(QHideEvent* event) {
    hideHelp();
    QPushButton::hideEvent(event);
}

bool StepHelpButton::eventFilter(QObject* watched, QEvent* event) {
    if (!m_popup || !m_popup->isVisible())
        return QPushButton::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) {
        QPoint position;
        if (event->type() == QEvent::MouseButtonPress)
            position = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        else {
            const auto* touch = static_cast<QTouchEvent*>(event);
            if (touch->points().isEmpty())
                return false;
            position = touch->points().first().globalPosition().toPoint();
        }
        if (!rect().contains(mapFromGlobal(position)) &&
            !m_popup->rect().contains(m_popup->mapFromGlobal(position)))
            hideHelp();
    } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        hideHelp();
        return true;
    } else if ((event->type() == QEvent::Resize || event->type() == QEvent::Hide) &&
               watched == m_popup->parentWidget())
        hideHelp();
    else if (event->type() == QEvent::Scroll) {
        // Scrolling the settings moves its anchor. Scrolls inside the help panel
        // retain it so long instructions can still be read on a small display.
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && !m_popup->isAncestorOf(widget))
            hideHelp();
    }
    return QPushButton::eventFilter(watched, event);
}

void furinaConfigureSettingsScroll(QScrollArea* scroll) {
    auto* viewport = scroll->viewport();
    viewport->setAttribute(Qt::WA_AcceptTouchEvents, true);
    QScroller::grabGesture(viewport, QScroller::TouchGesture);
    auto* scroller = QScroller::scroller(viewport);
    auto properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                               QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                               QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(properties);
    if (scroll->widget() && !scroll->widget()->findChildren<QTabWidget*>().isEmpty())
        new SettingsPageSizing(scroll);
}

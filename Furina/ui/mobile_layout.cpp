#include "mobile_layout.h"
#include <QMainWindow>
#include <QBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QScopedValueRollback>

MobileLayout::MobileLayout(QMainWindow* window) : QObject(window), m_window(window) {
    auto widget = [window](const char* name) { return window->findChild<QWidget*>(name); };
    auto layout = [window](const char* name) { return window->findChild<QBoxLayout*>(name); };
    m_sidebar = widget("sidebar");
    m_toolbar = widget("portraitToolbar");
    m_tabs = widget("portraitTabs");
    m_consentHost = widget("portraitConsentHost");
    m_consent = widget("consentPanel");
    m_bottom = widget("bottomPanel");
    m_direction = widget("directionPanel");
    m_debug = widget("debugPanel");
    m_preview = widget("viewPanel");
    m_debugTitle = widget("debugTitle");
    m_sideLayout = layout("toolbarLayout");
    m_toolLayout = layout("portraitToolbarLayout");
    m_contentLayout = layout("contentLayout");
    m_bottomLayout = layout("bottomLayout");
    m_debugLayout = layout("debugLayout");
    m_consentLayout = layout("portraitConsentLayout");
    m_modes = window->findChild<QCheckBox*>("showCasting");
    m_logs = window->findChild<QCheckBox*>("showDebug");
    m_start = window->findChild<QPushButton*>("start");
    m_settings = window->findChild<QPushButton*>("settings");
    m_maximize = window->findChild<QPushButton*>("maximize");
    auto* pages = new QButtonGroup(this);
    pages->setExclusive(true);
    pages->addButton(m_modes);
    pages->addButton(m_logs);
    connect(pages, &QButtonGroup::buttonToggled, this, [this](QAbstractButton*, bool checked) {
        if (checked)
            refresh(m_fullscreen);
    });
    for (const auto* name : {"receive", "share", "duplicate", "extend"})
        connect(window->findChild<QCheckBox*>(name), &QCheckBox::toggled, this,
                [this] { refresh(m_fullscreen); });
    refresh(false);
}

void MobileLayout::refresh(bool fullscreen) {
    if (m_arranging)
        return;
    QScopedValueRollback<bool> arranging(m_arranging, true);
    m_fullscreen = fullscreen;
    const bool portrait = m_window->width() < m_window->height();
    if (!m_initialized || portrait != m_portrait) {
        const bool consentVisible = !m_consent->isHidden();
        if (portrait) {
            m_toolLayout->insertWidget(0, m_start);
            m_toolLayout->addWidget(m_settings);
            m_toolLayout->addWidget(m_maximize);
            m_consentLayout->addWidget(m_consent);
        } else {
            m_sideLayout->insertWidget(0, m_start);
            m_sideLayout->addWidget(m_settings);
            m_sideLayout->addWidget(m_maximize);
            m_debugLayout->insertWidget(0, m_consent);
        }
        for (auto* button : {m_start, m_settings, m_maximize})
            button->show();
        m_consent->setVisible(consentVisible);
        m_portrait = portrait;
        m_initialized = true;
    }
    m_sidebar->setVisible(!portrait && !fullscreen);
    m_toolbar->setVisible(portrait && !fullscreen);
    m_tabs->setVisible(portrait && !fullscreen);
    m_consentHost->setVisible(portrait && !fullscreen);
    m_bottom->setVisible(!fullscreen);
    m_debugTitle->setVisible(!portrait);
    m_direction->setVisible(!portrait || m_modes->isChecked());
    m_debug->setVisible(!portrait || m_logs->isChecked());
    m_bottomLayout->setDirection(portrait ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_bottomLayout->setContentsMargins(portrait ? 12 : 16, portrait ? 6 : 12, portrait ? 12 : 16, 12);
    m_bottom->setMinimumHeight(portrait ? 150 : 220);
    m_bottom->setMaximumHeight(portrait ? QWIDGETSIZE_MAX : 220);
    // Leave room for the controls on small phones. Fullscreen removes all limits.
    const bool portraitExtension = m_window->findChild<QCheckBox*>("receive")->isChecked() &&
                                   m_window->findChild<QCheckBox*>("extend")->isChecked();
    const int previewHeight =
        portraitExtension
            ? qBound(140, qRound(m_window->width() * 1.6), qMax(140, m_window->height() - 280))
            : qBound(140, qRound(m_window->width() * 9.0 / 16.0), qMax(140, m_window->height() / 2));
    m_preview->setMinimumHeight(portrait && !fullscreen ? (portraitExtension ? 140 : previewHeight) : 0);
    m_preview->setMaximumHeight(portrait && !fullscreen ? previewHeight : QWIDGETSIZE_MAX);
    for (int i = 0; i < m_contentLayout->count(); ++i)
        m_contentLayout->setStretch(
            i, m_contentLayout->itemAt(i)->widget() ==
                       (portrait && !fullscreen && !portraitExtension ? m_bottom : m_preview)
                   ? 1
                   : 0);
    m_bottomLayout->setStretch(0, portrait ? 1 : 4);
    m_bottomLayout->setStretch(1, portrait ? 1 : 6);
}

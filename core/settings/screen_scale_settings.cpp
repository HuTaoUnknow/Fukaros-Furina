#include "screen_scale_settings.h"
#include "screen_scale.h"
#include <QDateTime>
#include <QSettings>
#include <QSignalBlocker>

ScreenScaleSettings::ScreenScaleSettings(Peer* peer, QWidget* window, bool desktop)
    : QObject(window), m_peer(peer), m_input(window->findChild<QComboBox*>("screenScale")),
      m_toggle(window->findChild<QCheckBox*>("screenScaleEnabled")), m_desktop(desktop) {
    Q_ASSERT(m_input);
    for (int i = 0; i < m_input->count(); ++i)
        m_input->setItemData(i, m_input->itemText(i).remove('%').toInt());
    QSettings s;
    const auto saved = s.value("screenScalePercent", 150).toUInt();
    m_percent = fr_screen_scale_valid(saved) ? int(saved) : 150;
    m_revision = s.value("screenScaleRevision", 0).toULongLong();
    m_enabled = s.value("screenScaleEnabled", true).toBool();
    updateControls();
    connect(m_input, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const int value = m_input->itemData(index).toInt();
        if (!fr_screen_scale_valid(value) || value == m_percent)
            return;
        m_percent = value;
        m_revision = qMax(m_revision + 1, quint64(QDateTime::currentMSecsSinceEpoch()));
        persist();
        send();
        if (m_enabled)
            emit changed(value);
    });
    if (m_toggle)
        connect(m_toggle, &QCheckBox::toggled, this, [this](bool enabled) {
            m_enabled = enabled;
            m_revision = qMax(m_revision + 1, quint64(QDateTime::currentMSecsSinceEpoch()));
            updateControls();
            persist();
            send();
            emit changed(percent());
        });
    connect(peer, &Peer::screenScaleAvailable, this, &ScreenScaleSettings::send);
    connect(peer, &Peer::screenScalePreference, this, &ScreenScaleSettings::receive);
}

void ScreenScaleSettings::updateControls() {
    const QSignalBlocker block(m_input);
    m_input->setCurrentIndex(m_input->findData(m_percent));
    m_input->setEnabled(m_enabled);
    if (m_toggle) {
        const QSignalBlocker toggleBlock(m_toggle);
        m_toggle->setChecked(m_enabled);
    }
}

void ScreenScaleSettings::persist() {
    QSettings s;
    s.setValue("screenScalePercent", m_percent);
    s.setValue("screenScaleRevision", m_revision);
    s.setValue("screenScaleEnabled", m_enabled);
}

void ScreenScaleSettings::send() {
    m_peer->sendScreenScale(m_percent, m_revision, m_enabled);
}

void ScreenScaleSettings::receive(int value, quint64 revision, bool enabled) {
    if (!fr_screen_scale_valid(value) || revision > quint64(INT64_MAX))
        return;
    if (revision < m_revision ||
        (revision == m_revision && m_desktop && (value != m_percent || enabled != m_enabled))) {
        send();
        return;
    }
    if (value == m_percent && revision == m_revision && enabled == m_enabled)
        return;
    const int previous = percent();
    m_percent = value;
    m_revision = revision;
    m_enabled = enabled;
    updateControls();
    persist();
    if (percent() != previous)
        emit changed(percent());
}

void ScreenScaleSettings::confirmApplied(int value) {
    // A successful 100% reset while disabled must not overwrite the saved zoom.
    if (!m_enabled || !fr_screen_scale_valid(value) || value == m_percent)
        return;
    m_percent = value;
    m_revision = qMax(m_revision + 1, quint64(QDateTime::currentMSecsSinceEpoch()));
    {
        QSignalBlocker block(m_input);
        m_input->setCurrentIndex(m_input->findData(value));
    }
    persist();
    send();
}

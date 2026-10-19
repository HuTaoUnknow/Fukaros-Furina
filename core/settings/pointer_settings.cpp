#include "pointer_settings.h"
#include <QDateTime>
#include <QSettings>
#include <QSignalBlocker>

PointerSettings::PointerSettings(Peer* peer, QWidget* window, bool desktop)
    : QObject(window), m_peer(peer), m_input(window->findChild<QDoubleSpinBox*>("pointerMultiplier")),
      m_desktop(desktop) {
    Q_ASSERT(m_input);
    QSettings s;
    m_percent = qBound(25, s.value("pointerPercent", 100).toInt(), 400);
    m_revision = s.value("pointerRevision", 0).toULongLong();
    m_input->setValue(multiplier());
    connect(m_input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        m_percent = qBound(25, qRound(value * 100), 400);
        m_revision = qMax(m_revision + 1, quint64(QDateTime::currentMSecsSinceEpoch()));
        persist();
        send();
        emit changed(multiplier());
    });
    m_mapping = window->findChild<QCheckBox*>("independentCursor");
    m_independent = s.value("independentCursor", true).toBool();
    m_mappingRevision = s.value("cursorMappingRevision", 0).toULongLong();
    if (m_mapping) {
        m_mapping->setChecked(m_independent);
        connect(m_mapping, &QCheckBox::toggled, this, [this](bool enabled) {
            m_independent = enabled;
            m_mappingRevision = qMax(m_mappingRevision + 1, quint64(QDateTime::currentMSecsSinceEpoch()));
            persistMapping();
            sendMapping();
            emit independentCursorChanged(enabled);
        });
    }
    connect(m_peer, &Peer::connected, this, &PointerSettings::sendMapping);
    connect(m_peer, &Peer::cursorPreference, this, &PointerSettings::receiveMapping);
    connect(m_peer, &Peer::connected, this, &PointerSettings::send);
    connect(m_peer, &Peer::pointerSpeed, this, &PointerSettings::receive);
}

void PointerSettings::persist() {
    QSettings s;
    s.setValue("pointerPercent", m_percent);
    s.setValue("pointerRevision", m_revision);
}

void PointerSettings::send() {
    m_peer->sendPointerSpeed(m_percent, m_revision);
}

void PointerSettings::receive(int percent, quint64 revision) {
    if (revision < m_revision || (revision == m_revision && m_desktop && percent != m_percent)) {
        send();
        return;
    }
    if (percent == m_percent && revision == m_revision)
        return;
    m_percent = percent;
    m_revision = revision;
    {
        QSignalBlocker block(m_input);
        m_input->setValue(multiplier());
    }
    persist();
    emit changed(multiplier());
}

void PointerSettings::persistMapping() {
    QSettings s;
    s.setValue("independentCursor", m_independent);
    s.setValue("cursorMappingRevision", m_mappingRevision);
}

void PointerSettings::sendMapping() {
    m_peer->sendCursorPreference(m_independent, m_mappingRevision);
}

void PointerSettings::receiveMapping(bool enabled, quint64 revision) {
    if (revision < m_mappingRevision ||
        (revision == m_mappingRevision && m_desktop && enabled != m_independent)) {
        sendMapping();
        return;
    }
    if (enabled == m_independent && revision == m_mappingRevision)
        return;
    m_independent = enabled;
    m_mappingRevision = revision;
    if (m_mapping) {
        QSignalBlocker block(m_mapping);
        m_mapping->setChecked(enabled);
    }
    persistMapping();
    emit independentCursorChanged(enabled);
}

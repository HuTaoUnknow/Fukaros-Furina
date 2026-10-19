#include "interface_support.h"
#include "video_policy.h"
#include <QSignalBlocker>

DirectionChoice::DirectionChoice(QWidget* parent)
    : QObject(parent), m_share(parent->findChild<QCheckBox*>("share")),
      m_receive(parent->findChild<QCheckBox*>("receive")), m_group(new QButtonGroup(this)) {
    m_group->setExclusive(true);
    m_group->addButton(m_share, 1);
    m_group->addButton(m_receive, 0);
    if (!m_share->isChecked() && !m_receive->isChecked())
        m_receive->setChecked(true);
    connect(m_group, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (checked)
            emit changed();
    });
}

bool DirectionChoice::sharing() const {
    return m_share->isChecked();
}

void DirectionChoice::setSharing(bool sharing) {
    const QSignalBlocker a(m_share), b(m_receive);
    (sharing ? m_share : m_receive)->setChecked(true);
}

ResolutionChoice::ResolutionChoice(QWidget* parent) : QObject(parent), m_group(new QButtonGroup(this)) {
    m_group->setExclusive(true);
    for (int edge : {1920, 1080, 720, 0})
        m_group->addButton(parent->findChild<QPushButton*>("resolution" + QString::number(edge)), edge);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int edge) {
        if (m_value != edge) {
            m_value = edge;
            emit changed(edge);
        }
    });
}

void ResolutionChoice::setValue(int edge) {
    if (!fr_resolution_valid(edge))
        edge = 0;
    const bool change = m_value != edge;
    m_value = edge;
    m_group->button(edge)->setChecked(true);
    if (change)
        emit changed(edge);
}

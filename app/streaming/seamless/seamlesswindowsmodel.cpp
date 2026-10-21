/**
 * @file app/streaming/seamless/seamlesswindowsmodel.cpp
 * @brief The page-facing list model over the applied seamless table.
 */
#include "seamlesswindowsmodel.h"


#include <QDebug>

SeamlessWindowsModel::SeamlessWindowsModel(QObject* parent, SeamlessModel* source)
    : QAbstractListModel(parent),
      m_Source(source != nullptr ? source : GetSeamlessModel())
{
    if (m_Source != nullptr) {
        // The table is applied on the frame thread; the rebuild is a queued
        // connection so the rows are replaced on the thread that owns this model.
        connect(m_Source, &SeamlessModel::windowsChanged,
                this, &SeamlessWindowsModel::onWindowsChanged,
                Qt::QueuedConnection);
    }
    onWindowsChanged();
}

int SeamlessWindowsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_Rows.size();
}

QVariant SeamlessWindowsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_Rows.size()) {
        return {};
    }

    const Row& row = m_Rows.at(index.row());
    switch (role) {
    case IdRole:
        return row.id;
    case TitleRole:
        return row.title;
    case RectRole:
        return row.rect;
    case FlagsRole:
        return row.flags;
    case StateRole:
        return row.state;
    case ZRole:
        return row.z;
    case IconRole:
        return row.icon;
    default:
        return {};
    }
}

QHash<int, QByteArray> SeamlessWindowsModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {TitleRole, "title"},
        {RectRole, "rect"},
        {FlagsRole, "flags"},
        {StateRole, "state"},
        {ZRole, "z"},
        {IconRole, "icon"},
    };
}

void SeamlessWindowsModel::onWindowsChanged()
{
    QVector<Row> rows;
    const QString appTitle = SeamlessSessionAppName();

    if (m_Source != nullptr) {
        const SeamlessModel::TableView view = m_Source->snapshotTable();
        rows.reserve(view.windows.size());
        for (const auto& entry : view.windows) {
            Row row;
            row.id = entry.id;
            // The local rectangle: the cell's canvas position with the client size
            // the user's window is drawn at.
            row.rect = QRect(entry.cellPos, entry.clientSize);
            row.flags = entry.flags;
            // Bits 5..6 are the window state (normal, minimized, maximized).
            row.state = (entry.flags >> 5) & 0x3;
            row.z = entry.z;
            // One name for the session: the table is scoped to the launched app by
            // the caller's only_pid filter, so the session's app name is the label.
            // Per-app titles would need a wire field and the wire stays frozen. When
            // the session has no launched pid (only_pid == 0, no filter) the rows are
            // desktop-wide and there is no app name to show: they are labelled
            // plainly rather than with a name that would be wrong.
            row.title = appTitle.isEmpty() ? tr("Window") : appTitle;
            // The 0x56 icon, when the host has sent one for this hash, is served
            // by the image provider; an empty value lets the page fall back to the
            // app-list boxart and then the letter tile.
            QByteArray iconBytes;
            if (entry.iconHash != 0 && SeamlessIconBytes(entry.iconHash, iconBytes)) {
                row.icon = QUrl(QStringLiteral("image://seamlessicon/") +
                                QString::number(entry.iconHash, 16));
            }
            else {
                row.icon = QUrl();
            }
            rows.push_back(row);
        }
    }

    beginResetModel();
    m_Rows = rows;
    endResetModel();
    emit countChanged();
}

void SeamlessWindowsModel::focusWindow(int row)
{
    if (row < 0 || row >= m_Rows.size()) {
        return;
    }

    // 0x52 carries the table index of the window to focus: the same byte a click
    // into the local window sends, so the host has one focus path.
    uint8_t message[SEAMLESS_INPUT_SIZE] {};
    message[0] = SEAMLESS_MSG_WINDOW_INPUT;
    message[1] = (uint8_t) row;
    message[2] = SEAMLESS_INPUT_FOCUS_RAISE;
    LiSendSeamlessMessage(message, (int) sizeof(message));
}

void SeamlessWindowsModel::closeWindow(int row)
{
    if (row < 0 || row >= m_Rows.size()) {
        return;
    }

    // 0x58 kind 5: the close intent the local window's close button sends.
    uint8_t message[SEAMLESS_INTENT_SIZE] {};
    message[0] = SEAMLESS_MSG_WINDOW_INTENT;
    message[1] = (uint8_t) row;
    message[2] = SEAMLESS_INTENT_CLOSE;
    LiSendSeamlessMessage(message, (int) sizeof(message));
}

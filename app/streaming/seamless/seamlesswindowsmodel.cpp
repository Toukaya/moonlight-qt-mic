/**
 * @file app/streaming/seamless/seamlesswindowsmodel.cpp
 * @brief The page-facing list model over the applied seamless table.
 */
#include "seamlesswindowsmodel.h"

#include "streaming/session.h"

#include <QDebug>

SeamlessWindowsModel::SeamlessWindowsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_Source = GetSeamlessModel();
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

    if (m_Source != nullptr) {
        const SeamlessModel::TableView view = m_Source->snapshotTable();
        rows.reserve(view.windows.size());
        for (const auto& entry : view.windows) {
            Row row;
            row.id = entry.id;
            row.rect = QRect(entry.cellPos, entry.cellSize);
            row.flags = entry.flags;
            // Bits 5..6 are the window state (normal, minimized, maximized).
            row.state = (entry.flags >> 5) & 0x3;
            row.z = entry.z;
            // The wire carries a title hash, not the text: the role exists and is
            // stable per window, and it is the hash until a title source exists.
            row.title = QStringLiteral("0x") + QString::number(entry.titleHash, 16).rightJustified(8, QLatin1Char('0'));
            // 0x56 icons are not uplinked yet; an empty icon lets the page fall
            // back to the app-list boxart, and to the letter tile while loading.
            row.icon = QUrl();
            rows.push_back(row);
        }
    }

    beginResetModel();
    m_Rows = rows;
    endResetModel();
    emit countChanged();
}

/**
 * @file app/streaming/seamless/seamlesswindowsmodel.h
 * @brief The applied window table, exposed to the page as a list model.
 * @details The page binds to this by property name: roles id, title, rect, flags,
 *          state, z and icon. The rows are rebuilt from the applied table (never
 *          the pending one), on the thread that owns the model, whenever the
 *          model reports that a table was applied.
 */
#pragma once

#include <QAbstractListModel>
#include <QRect>
#include <QUrl>

#include "seamlessmodel.h"

// Provided by the session: the launched app's name and the icon bytes the host
// sent. Declared here so this model (and a headless test of it) does not need the
// whole session header.
class QByteArray;
SeamlessModel* GetSeamlessModel();
QString SeamlessSessionAppName();
bool SeamlessIconBytes(quint32 iconHash, QByteArray& out);

class SeamlessWindowsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        /// The LOCAL rectangle: the canvas cell position with the client (local)
        /// size, i.e. where and how large the local window is. Not the canvas cell
        /// size and not the desktop rectangle - those are the other two rects the
        /// entry carries.
        RectRole,
        FlagsRole,
        StateRole,
        ZRole,
        IconRole,
    };
    Q_ENUM(Role)

    // source defaults to the session's live model; a test can hand in its own.
    explicit SeamlessWindowsModel(QObject* parent = nullptr, SeamlessModel* source = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_Rows.size(); }

    /// The presentation gate: the page that launches a session sets this, the
    /// classic routes never do. It lives outside the renderer.
    Q_INVOKABLE void setPresentationMode(bool enabled);

    /// Asks the host to focus this row's window: a 0x52 with the row's table
    /// index, the same request a click into the local window sends.
    Q_INVOKABLE void focusWindow(int row);
    /// Asks the host to close this row's window: a 0x58 with kind 5, the same
    /// request the local window's close button sends.
    Q_INVOKABLE void closeWindow(int row);

signals:
    void countChanged();

private slots:
    void onWindowsChanged();

private:
    struct Row {
        quint32 id = 0;
        QString title;
        QRect rect;
        quint32 flags = 0;
        uint state = 0;
        int z = 0;
        QUrl icon;
    };

    SeamlessModel* m_Source = nullptr;
    QVector<Row> m_Rows;
};

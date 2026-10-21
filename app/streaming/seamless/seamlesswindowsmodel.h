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

class SeamlessWindowsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        RectRole,
        FlagsRole,
        StateRole,
        ZRole,
        IconRole,
    };
    Q_ENUM(Role)

    explicit SeamlessWindowsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_Rows.size(); }

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

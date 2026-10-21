/**
 * @file app/streaming/seamless/seamlessmodel.h
 * @brief The window table the host published, and when the client applies it.
 * @details A table is applied at a frame boundary and nowhere else. A decoded
 *          frame and the cells it was encoded with must always match, so the
 *          newest table is held back until the next frame arrives, and the
 *          acknowledgement goes out only when that frame is applied.
 */
#pragma once

#include <QMutex>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QVector>

extern "C" {
#include <Limelight.h>
}

class SeamlessModel : public QObject
{
    Q_OBJECT

public:
    struct WindowEntry {
        quint32 id = 0;
        QPoint cellPos;
        QSize cellSize;
        QSize clientSize;
        QPoint desktopPos;
        quint8 z = 0;
        quint8 flags = 0;
        quint32 titleHash = 0;
        quint32 iconHash = 0;
    };

    explicit SeamlessModel(QObject* parent = nullptr);

    // Called on the network thread when a table message arrives. The kind says
    // whether the entries are the whole set (a snapshot) or only the ones that
    // changed (a delta); a delta cannot be understood without the held set.
    void onTable(quint8 seq, quint8 kind, quint16 canvasW, quint16 canvasH,
                 qint16 workAreaX, qint16 workAreaY, quint16 workAreaW, quint16 workAreaH,
                 const WindowEntry* entries, int count);

    // Called when a decoded frame reaches the boundary. This is the only place a
    // table becomes the table the client draws with.
    void onFrame(quint32 frameId);

    /**
     * @brief Merge the entries of a delta into the table the client holds.
     * @return false when the delta names a window the held table does not have.
     * @details A delta carries no membership change: the host sends a snapshot
     *          when the set gains or loses a window, so an unknown id is a
     *          version this client cannot apply and must be rejected rather
     *          than guessed at. The result is sorted by z, the draw order.
     */
    static bool mergeDelta(const QVector<WindowEntry>& held, const QVector<WindowEntry>& changed,
                           QVector<WindowEntry>* merged);

    /// One applied version: the table and the canvas it is measured against,
    /// taken together so a render thread can never pair N+1 cells with N canvas.
    struct TableView {
        QVector<WindowEntry> windows;
        quint16 canvasW = 0;
        quint16 canvasH = 0;
    };

    // A render thread must not iterate the applied table directly: the decoder
    // thread replaces it at every frame boundary. This takes one copy under one
    // lock acquisition, canvas included.
    TableView snapshotTable() const;
    quint8 appliedSeq() const { return m_AppliedSeq; }
    int appliedCount() const { return m_AppliedCount; }
    // The last answer this model sent, kept so an applied table and its answer
    // can be read as one fact instead of two.
    quint8 lastAckSeq() const { return m_LastAckSeq; }
    quint8 lastAckResult() const { return m_LastAckResult; }

signals:
    void windowsChanged();

private:
    void applyPending();

    // The pending table crosses threads: it is written on the network thread
    // when a version arrives and taken on the frame thread when a frame is
    // applied. This lock is the only thing that makes that handoff whole; the
    // applied table itself is written on the frame thread alone.
    mutable QMutex m_Lock;

    QVector<WindowEntry> m_Windows;
    QVector<WindowEntry> m_Pending;
    quint8 m_PendingKind = SEAMLESS_TABLE_SNAPSHOT;
    bool m_HasPending = false;
    quint8 m_PendingSeq = 0;
    quint8 m_AppliedSeq = 0;
    int m_AppliedCount = 0;
    quint8 m_LastAckSeq = 0;
    quint8 m_LastAckResult = SEAMLESS_ACK_APPLIED;
    quint16 m_CanvasW = 0;
    quint16 m_CanvasH = 0;
    quint16 m_PendingCanvasW = 0;
    quint16 m_PendingCanvasH = 0;
    qint16 m_WorkX = 0;
    qint16 m_WorkY = 0;
    quint16 m_WorkW = 0;
    quint16 m_WorkH = 0;
};

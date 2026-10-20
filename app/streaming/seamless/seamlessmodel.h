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

    // Called on the network thread when a table message arrives.
    void onTable(quint8 seq, quint16 canvasW, quint16 canvasH,
                 qint16 workAreaX, qint16 workAreaY, quint16 workAreaW, quint16 workAreaH,
                 const WindowEntry* entries, int count);

    // Called when a decoded frame reaches the boundary. This is the only place a
    // table becomes the table the client draws with.
    void onFrame(quint32 frameId);

    quint16 canvasWidth() const { return m_CanvasW; }
    quint16 canvasHeight() const { return m_CanvasH; }
    const QVector<WindowEntry>& windows() const { return m_Windows; }
    quint8 appliedSeq() const { return m_AppliedSeq; }
    int appliedCount() const { return m_AppliedCount; }

signals:
    void windowsChanged();

private:
    void applyPending();

    // The pending table crosses threads: it is written on the network thread
    // when a version arrives and taken on the frame thread when a frame is
    // applied. This lock is the only thing that makes that handoff whole; the
    // applied table itself is written on the frame thread alone.
    QMutex m_Lock;

    QVector<WindowEntry> m_Windows;
    QVector<WindowEntry> m_Pending;
    bool m_HasPending = false;
    quint8 m_PendingSeq = 0;
    quint8 m_AppliedSeq = 0;
    int m_AppliedCount = 0;
    quint16 m_CanvasW = 0;
    quint16 m_CanvasH = 0;
    quint16 m_PendingCanvasW = 0;
    quint16 m_PendingCanvasH = 0;
    qint16 m_WorkX = 0;
    qint16 m_WorkY = 0;
    quint16 m_WorkW = 0;
    quint16 m_WorkH = 0;
};

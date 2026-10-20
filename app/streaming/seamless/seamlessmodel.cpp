/**
 * @file app/streaming/seamless/seamlessmodel.cpp
 * @brief The window table, applied only at a frame boundary.
 */
#include "seamlessmodel.h"

#include <QDebug>
#include <QMutexLocker>

SeamlessModel::SeamlessModel(QObject* parent) : QObject(parent)
{
}

void SeamlessModel::onTable(quint8 seq, quint16 canvasW, quint16 canvasH,
                            qint16 workAreaX, qint16 workAreaY, quint16 workAreaW, quint16 workAreaH,
                            const WindowEntry* entries, int count)
{
    // The protocol already rejected anything over the window limit or out of
    // bounds, so this is a plain copy into the pending slot. The over-limit
    // answer is sent before the lock is taken: it does not touch the model.
    if (count < 0 || count > SEAMLESS_MAX_WINDOWS) {
        LiSendWindowAck(seq, SEAMLESS_ACK_OVER_LIMIT);
        return;
    }

    QMutexLocker locker(&m_Lock);

    m_Pending = QVector<WindowEntry>(entries, entries + count);
    m_PendingSeq = seq;
    m_PendingCanvasW = canvasW;
    m_PendingCanvasH = canvasH;
    m_WorkX = workAreaX;
    m_WorkY = workAreaY;
    m_WorkW = workAreaW;
    m_WorkH = workAreaH;
    m_HasPending = true;

    // The acknowledgement is sent when the table is applied, not when it
    // arrives: the host must not advance the sequence on the strength of a
    // table the client is still holding back.
}

void SeamlessModel::onFrame(quint32 frameId)
{
    Q_UNUSED(frameId);

    applyPending();
}

void SeamlessModel::applyPending()
{
    QVector<WindowEntry> pending;
    quint8 pendingSeq = 0;
    quint16 pendingCanvasW = 0;
    quint16 pendingCanvasH = 0;
    qint16 workX = 0;
    qint16 workY = 0;
    quint16 workW = 0;
    quint16 workH = 0;

    // The handoff of the pending table is the only part that crosses threads, so
    // it is the only part under the lock: the network thread may be writing a
    // newer version while this frame is being applied.
    {
        QMutexLocker locker(&m_Lock);

        if (!m_HasPending) {
            return;
        }

        m_HasPending = false;
        pending = m_Pending;
        pendingSeq = m_PendingSeq;
        pendingCanvasW = m_PendingCanvasW;
        pendingCanvasH = m_PendingCanvasH;
        workX = m_WorkX;
        workY = m_WorkY;
        workW = m_WorkW;
        workH = m_WorkH;
    }

    const bool canvasMoved = (m_CanvasW != pendingCanvasW) || (m_CanvasH != pendingCanvasH);

    m_Windows = pending;
    m_CanvasW = pendingCanvasW;
    m_CanvasH = pendingCanvasH;
    m_WorkX = workX;
    m_WorkY = workY;
    m_WorkW = workW;
    m_WorkH = workH;
    m_AppliedSeq = pendingSeq;
    m_AppliedCount = m_Windows.size();

    qInfo() << "Seamless: table" << m_AppliedSeq << "applied at a frame boundary,"
            << m_AppliedCount << "windows, canvas" << m_CanvasW << "x" << m_CanvasH
            << "work area" << m_WorkX << m_WorkY << m_WorkW << m_WorkH;

    // Only now does the host learn that this version can be superseded.
    LiSendWindowAck(m_AppliedSeq, SEAMLESS_ACK_APPLIED);

    Q_UNUSED(canvasMoved);
    emit windowsChanged();
}

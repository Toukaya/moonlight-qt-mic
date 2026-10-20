/**
 * @file app/streaming/seamless/seamlessmodel.cpp
 * @brief The window table, applied only at a frame boundary.
 */
#include "seamlessmodel.h"

#include <QDebug>

SeamlessModel::SeamlessModel(QObject* parent) : QObject(parent)
{
}

void SeamlessModel::onTable(quint8 seq, quint16 canvasW, quint16 canvasH,
                            qint16 workAreaX, qint16 workAreaY, quint16 workAreaW, quint16 workAreaH,
                            const WindowEntry* entries, int count)
{
    // The protocol already rejected anything over the window limit or out of
    // bounds, so this is a plain copy into the pending slot.
    if (count < 0 || count > SEAMLESS_MAX_WINDOWS) {
        LiSendWindowAck(seq, SEAMLESS_ACK_OVER_LIMIT);
        return;
    }

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

    if (!m_HasPending) {
        return;
    }

    applyPending();
}

void SeamlessModel::applyPending()
{
    m_HasPending = false;

    const bool canvasMoved = (m_CanvasW != m_PendingCanvasW) || (m_CanvasH != m_PendingCanvasH);

    m_Windows = m_Pending;
    m_CanvasW = m_PendingCanvasW;
    m_CanvasH = m_PendingCanvasH;
    m_AppliedSeq = m_PendingSeq;
    m_AppliedCount = m_Windows.size();

    qInfo() << "Seamless: table" << m_AppliedSeq << "applied at a frame boundary,"
            << m_AppliedCount << "windows, canvas" << m_CanvasW << "x" << m_CanvasH
            << "work area" << m_WorkX << m_WorkY << m_WorkW << m_WorkH;

    // Only now does the host learn that this version can be superseded.
    LiSendWindowAck(m_AppliedSeq, SEAMLESS_ACK_APPLIED);

    Q_UNUSED(canvasMoved);
    emit windowsChanged();
}

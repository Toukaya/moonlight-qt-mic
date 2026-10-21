/**
 * @file app/streaming/seamless/seamlessmodel.cpp
 * @brief The window table, applied only at a frame boundary.
 */
#include "seamlessmodel.h"

#include <algorithm>

#include <QDebug>
#include <QMutexLocker>

SeamlessModel::SeamlessModel(QObject* parent) : QObject(parent)
{
}

void SeamlessModel::onTable(quint8 seq, quint8 kind, quint16 canvasW, quint16 canvasH,
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
    m_PendingKind = kind;
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
    quint8 pendingKind = SEAMLESS_TABLE_SNAPSHOT;
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
        pendingKind = m_PendingKind;
        pendingSeq = m_PendingSeq;
        pendingCanvasW = m_PendingCanvasW;
        pendingCanvasH = m_PendingCanvasH;
        workX = m_WorkX;
        workY = m_WorkY;
        workW = m_WorkW;
        workH = m_WorkH;
    }

    QVector<WindowEntry> applied;
    quint16 canvasW = pendingCanvasW;
    quint16 canvasH = pendingCanvasH;

    if (pendingKind == SEAMLESS_TABLE_DELTA) {
        // A delta changes entries of the table the client holds. The canvas is
        // the one the last snapshot named: the delta header does not repeat it,
        // so the decoder gives it as zero and it must not overwrite the held one.
        if (!mergeDelta(m_Windows, pending, &applied)) {
            // The delta names a window this client does not hold, so it cannot be
            // applied. Saying APPLIED here would be a lie on the wire that the
            // host would never repair, because a geometry-only change never
            // forces a snapshot. The host turns a rejection into one.
            m_LastAckSeq = pendingSeq;
            m_LastAckResult = SEAMLESS_ACK_REJECTED;
            qWarning() << "Seamless: delta" << pendingSeq
                       << "rejected, it names a window the applied table does not hold";
            LiSendWindowAck(pendingSeq, SEAMLESS_ACK_REJECTED);
            return;
        }

        canvasW = m_CanvasW;
        canvasH = m_CanvasH;
    }
    else {
        applied = pending;
    }

    // The apply point and the acknowledgement timing are unchanged; the lock is
    // only here because a render thread takes a snapshot of this state while the
    // frame thread replaces it.
    {
        QMutexLocker locker(&m_Lock);
        m_Windows = applied;
        m_CanvasW = canvasW;
        m_CanvasH = canvasH;
        m_WorkX = workX;
        m_WorkY = workY;
        m_WorkW = workW;
        m_WorkH = workH;
        m_AppliedSeq = pendingSeq;
        m_AppliedCount = m_Windows.size();
    }

    qInfo() << "Seamless: table" << m_AppliedSeq
            << (pendingKind == SEAMLESS_TABLE_DELTA ? "delta" : "snapshot")
            << "applied at a frame boundary,"
            << m_AppliedCount << "windows, canvas" << m_CanvasW << "x" << m_CanvasH
            << "work area" << m_WorkX << m_WorkY << m_WorkW << m_WorkH;

    // Only now does the host learn that this version can be superseded.
    m_LastAckSeq = m_AppliedSeq;
    m_LastAckResult = SEAMLESS_ACK_APPLIED;
    LiSendWindowAck(m_AppliedSeq, SEAMLESS_ACK_APPLIED);

    emit windowsChanged();
}

SeamlessModel::TableView SeamlessModel::snapshotTable() const
{
    QMutexLocker locker(&m_Lock);
    TableView view;
    view.windows = m_Windows;
    view.canvasW = m_CanvasW;
    view.canvasH = m_CanvasH;
    return view;
}

bool SeamlessModel::mergeDelta(const QVector<WindowEntry>& held, const QVector<WindowEntry>& changed,
                               QVector<WindowEntry>* merged)
{
    QVector<WindowEntry> result = held;

    for (const auto& entry : changed) {
        bool found = false;
        for (int i = 0; i < result.size(); i++) {
            if (result[i].id == entry.id) {
                result[i] = entry;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    // The wire carries one z per entry, and the draw order is the table's.
    std::sort(result.begin(), result.end(),
              [](const WindowEntry& a, const WindowEntry& b) { return a.z < b.z; });

    *merged = result;
    return true;
}

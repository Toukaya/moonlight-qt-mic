// Ported from wjbeckett/artemis (GPL).
#include "clipboardmanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"
#include "settings/artemissettings.h"
#include <QGuiApplication>
#include <QMimeData>
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHttpMultiPart>
#include <QCryptographicHash>

const QString ClipboardManager::CLIPBOARD_IDENTIFIER = "moonlight_qt_clipboard_sync";

ClipboardManager* ClipboardManager::s_instance = nullptr;

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
    , m_computer(nullptr)
    , m_http(nullptr)
    , m_smartSyncEnabled(false)
    , m_bidirectionalSync(true)
    , m_showToast(true)
    , m_hideContent(false)
    , m_maxClipboardSize(DEFAULT_MAX_SIZE)
    , m_enabled(false)
    , m_connected(false)
    , m_textOnlyMode(true)
    , m_maxContentSizeMB(1)
    , m_showNotifications(true)
    , m_syncInProgress(false)
{
    connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardManager::onClipboardChanged);
    loadSettings();
}

void ClipboardManager::loadSettings()
{
    auto settings = ArtemisSettings::instance();
    m_enabled = settings->clipboardSyncEnabled();
    m_smartSyncEnabled = m_enabled;
    m_bidirectionalSync = settings->clipboardSyncBidirectional();
    m_maxClipboardSize = settings->clipboardSyncMaxSize();
    m_maxContentSizeMB = m_maxClipboardSize / (1024 * 1024);

    qDebug() << "ClipboardManager: Loaded settings - enabled:" << m_enabled
             << "bidirectional:" << m_bidirectionalSync
             << "maxSize:" << m_maxClipboardSize << "bytes";
}

ClipboardManager::~ClipboardManager()
{
    disconnect();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ClipboardManager* ClipboardManager::instance()
{
    if (!s_instance) {
        s_instance = new ClipboardManager();
    }
    return s_instance;
}

QObject* ClipboardManager::qmlInstance(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    return instance();
}

void ClipboardManager::setConnection(NvComputer *computer, NvHTTP *http)
{
    m_computer = computer;
    m_http = http;

    bool wasConnected = m_connected;
    m_connected = (computer != nullptr && http != nullptr);

    if (wasConnected != m_connected) {
        emit connectionChanged();
    }

    qDebug() << "ClipboardManager: Connected to" << (computer ? computer->name : "null");

    bool supported = isClipboardSyncSupported();
    emit apolloSupportChanged(supported);
}

void ClipboardManager::disconnect()
{
    m_computer = nullptr;
    m_http = nullptr;
    m_syncInProgress = false;

    bool wasConnected = m_connected;
    m_connected = false;

    if (wasConnected) {
        emit connectionChanged();
    }

    emit apolloSupportChanged(false);
}

bool ClipboardManager::sendClipboard(bool force)
{
    if (!m_http || !m_computer) {
        qWarning() << "ClipboardManager: No connection available for clipboard sync";
        return false;
    }

    if (!isClipboardSyncSupported()) {
        emit clipboardSyncFailed("Clipboard sync only works with Apollo/Sunshine servers");
        return false;
    }

    if (m_syncInProgress) {
        return false;
    }

    QString clipboardText = getClipboardContent(force);
    if (clipboardText.isNull()) {
        return false;
    }

    return sendClipboardToServer(clipboardText);
}

bool ClipboardManager::getClipboard()
{
    if (!m_http || !m_computer) {
        qWarning() << "ClipboardManager: No connection available for clipboard sync";
        return false;
    }

    if (!isClipboardSyncSupported()) {
        emit clipboardSyncFailed("Clipboard sync only works with Apollo/Sunshine servers");
        return false;
    }

    if (m_syncInProgress) {
        return false;
    }

    QString clipboardContent = getClipboardFromServer();
    if (!clipboardContent.isNull()) {
        setClipboardContent(clipboardContent);
        return true;
    }

    return false;
}

void ClipboardManager::enableSmartSync(bool enabled)
{
    m_smartSyncEnabled = enabled;
}

bool ClipboardManager::isSmartSyncEnabled() const
{
    return m_smartSyncEnabled;
}

bool ClipboardManager::isClipboardSyncSupported() const
{
    if (!m_computer || !m_http) {
        return false;
    }
    // Apollo/Sunshine endpoint detection happens via HTTP success/failure.
    return true;
}

void ClipboardManager::onStreamStarted()
{
    if (m_smartSyncEnabled) {
        sendClipboard(false);
    }
}

void ClipboardManager::onStreamResumed()
{
    if (m_smartSyncEnabled) {
        sendClipboard(false);
    }
}

void ClipboardManager::onFocusLost()
{
    if (m_smartSyncEnabled && m_bidirectionalSync) {
        getClipboard();
    }
}

void ClipboardManager::setMaxClipboardSize(int maxSize)
{
    m_maxClipboardSize = maxSize;
}

int ClipboardManager::getMaxClipboardSize() const
{
    return m_maxClipboardSize;
}

void ClipboardManager::setBidirectionalSync(bool enabled)
{
    if (m_bidirectionalSync != enabled) {
        m_bidirectionalSync = enabled;

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncBidirectional(enabled);
        settings->save();

        emit bidirectionalSyncChanged();
    }
}

bool ClipboardManager::isBidirectionalSyncEnabled() const
{
    return m_bidirectionalSync;
}

void ClipboardManager::setShowToast(bool enabled)
{
    m_showToast = enabled;
}

bool ClipboardManager::shouldShowToast() const
{
    return m_showToast;
}

void ClipboardManager::setHideContent(bool enabled)
{
    m_hideContent = enabled;
}

bool ClipboardManager::shouldHideContent() const
{
    return m_hideContent;
}

void ClipboardManager::onClipboardChanged()
{
    if (m_syncInProgress) {
        return;
    }

    QString content = getClipboardContent(false);
    if (!content.isNull() && !isOwnClipboardChange(content)) {
        emit clipboardContentChanged();

        if (m_smartSyncEnabled) {
            sendClipboard(false);
        }
    }
}

QString ClipboardManager::getClipboardContent(bool force)
{
    if (!m_clipboard->mimeData()) {
        return QString();
    }

    const QMimeData *mimeData = m_clipboard->mimeData();
    if (!mimeData->hasText()) {
        return QString();
    }

    QString text = mimeData->text();

    if (text.size() > m_maxClipboardSize) {
        qWarning() << "ClipboardManager: Clipboard content too large:" << text.size() << "bytes";
        return QString();
    }

    if (!force && isOwnClipboardChange(text)) {
        return QString();
    }

    return text;
}

void ClipboardManager::setClipboardContent(const QString &content)
{
    if (content.isEmpty()) {
        return;
    }

    m_syncInProgress = true;
    markAsOwnContent(content);

    QMimeData *mimeData = new QMimeData();
    mimeData->setText(content);

    if (m_hideContent) {
        mimeData->setData("application/x-qt-windows-mime;value=\"Clipboard Viewer Format\"", QByteArray());
    }

    m_clipboard->setMimeData(mimeData);
    m_lastReceivedContent = content;

    m_syncInProgress = false;
}

bool ClipboardManager::isOwnClipboardChange(const QString &content)
{
    QString hash = generateContentHash(content);
    return m_ownContentHashes.contains(hash) || content == m_lastReceivedContent;
}

void ClipboardManager::markAsOwnContent(const QString &content)
{
    QString hash = generateContentHash(content);
    m_ownContentHashes.append(hash);

    if (m_ownContentHashes.size() > 10) {
        m_ownContentHashes.removeFirst();
    }
}

QString ClipboardManager::generateContentHash(const QString &content)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(content.toUtf8());
    return hash.result().toHex();
}

bool ClipboardManager::sendClipboardToServer(const QString &content)
{
    if (!m_http || content.isEmpty()) {
        return false;
    }

    emit clipboardSyncStarted();
    m_syncInProgress = true;

    bool success = m_http->sendClipboardContent(content);

    m_syncInProgress = false;

    if (success) {
        markAsOwnContent(content);
        emit clipboardSyncCompleted();

        if (m_showToast) {
            emit showToast("Clipboard uploaded to server");
        }

        return true;
    } else {
        emit clipboardSyncFailed("Failed to send clipboard to server");
        return false;
    }
}

QString ClipboardManager::getClipboardFromServer()
{
    if (!m_http) {
        return QString();
    }

    emit clipboardSyncStarted();
    m_syncInProgress = true;

    QString content = m_http->getClipboardContent();

    m_syncInProgress = false;

    if (!content.isEmpty()) {
        emit clipboardSyncCompleted();

        if (m_showToast) {
            emit showToast("Clipboard downloaded from server");
        }

        return content;
    } else {
        emit clipboardSyncFailed("Failed to get clipboard from server");
        return QString();
    }
}

void ClipboardManager::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        enableSmartSync(enabled);

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncEnabled(enabled);
        settings->save();

        emit enabledChanged();
    }
}

void ClipboardManager::setTextOnlyMode(bool textOnly)
{
    if (m_textOnlyMode != textOnly) {
        m_textOnlyMode = textOnly;
        emit textOnlyModeChanged();
    }
}

void ClipboardManager::setMaxContentSizeMB(int sizeMB)
{
    if (m_maxContentSizeMB != sizeMB) {
        m_maxContentSizeMB = sizeMB;
        m_maxClipboardSize = sizeMB * 1024 * 1024;

        auto settings = ArtemisSettings::instance();
        settings->setClipboardSyncMaxSize(m_maxClipboardSize);
        settings->save();

        emit maxContentSizeMBChanged();
    }
}

void ClipboardManager::setShowNotifications(bool show)
{
    if (m_showNotifications != show) {
        m_showNotifications = show;
        m_showToast = show;
        emit showNotificationsChanged();
    }
}

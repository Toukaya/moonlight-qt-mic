// Ported from wjbeckett/artemis (GPL). Manages bidirectional clipboard sync
// with Apollo/Sunshine servers via the /actions/clipboard HTTP endpoint.
#pragma once

#include <QObject>
#include <QClipboard>
#include <QTimer>
#include <QQmlEngine>
#include <QCryptographicHash>

class NvComputer;
class NvHTTP;

#ifndef NVCOMPUTER_OPAQUE_DECLARED
#define NVCOMPUTER_OPAQUE_DECLARED
Q_DECLARE_OPAQUE_POINTER(NvComputer*)
#endif

#ifndef NVHTTP_OPAQUE_DECLARED
#define NVHTTP_OPAQUE_DECLARED
Q_DECLARE_OPAQUE_POINTER(NvHTTP*)
#endif

class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isEnabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool textOnlyMode READ textOnlyMode WRITE setTextOnlyMode NOTIFY textOnlyModeChanged)
    Q_PROPERTY(int maxContentSizeMB READ maxContentSizeMB WRITE setMaxContentSizeMB NOTIFY maxContentSizeMBChanged)
    Q_PROPERTY(bool showNotifications READ showNotifications WRITE setShowNotifications NOTIFY showNotificationsChanged)
    Q_PROPERTY(bool bidirectionalSync READ isBidirectionalSyncEnabled WRITE setBidirectionalSync NOTIFY bidirectionalSyncChanged)

public:
    explicit ClipboardManager(QObject *parent = nullptr);
    ~ClipboardManager();

    static ClipboardManager* instance();
    static QObject* qmlInstance(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool isEnabled() const { return m_enabled; }
    bool isConnected() const { return m_connected; }
    bool textOnlyMode() const { return m_textOnlyMode; }
    int maxContentSizeMB() const { return m_maxContentSizeMB; }
    bool showNotifications() const { return m_showNotifications; }

    void setEnabled(bool enabled);
    void setTextOnlyMode(bool textOnly);
    void setMaxContentSizeMB(int sizeMB);
    void setShowNotifications(bool show);

    Q_INVOKABLE void setConnection(NvComputer *computer, NvHTTP *http);
    Q_INVOKABLE void disconnect();

    Q_INVOKABLE bool sendClipboard(bool force = false);
    Q_INVOKABLE bool getClipboard();

    Q_INVOKABLE void enableSmartSync(bool enabled);
    Q_INVOKABLE bool isSmartSyncEnabled() const;

    Q_INVOKABLE bool isClipboardSyncSupported() const;

    Q_INVOKABLE void onStreamStarted();
    Q_INVOKABLE void onStreamResumed();
    Q_INVOKABLE void onFocusLost();

    Q_INVOKABLE void setMaxClipboardSize(int maxSize);
    Q_INVOKABLE int getMaxClipboardSize() const;

    Q_INVOKABLE void setBidirectionalSync(bool enabled);
    Q_INVOKABLE bool isBidirectionalSyncEnabled() const;

    Q_INVOKABLE void setShowToast(bool enabled);
    Q_INVOKABLE bool shouldShowToast() const;

    Q_INVOKABLE void setHideContent(bool enabled);
    Q_INVOKABLE bool shouldHideContent() const;

signals:
    void clipboardSyncStarted();
    void clipboardSyncCompleted();
    void clipboardSyncFailed(const QString &error);
    void clipboardContentChanged();
    void showToast(const QString &message);
    void apolloSupportChanged(bool supported);

    void enabledChanged();
    void connectionChanged();
    void textOnlyModeChanged();
    void maxContentSizeMBChanged();
    void showNotificationsChanged();
    void bidirectionalSyncChanged();

private slots:
    void onClipboardChanged();

private:
    void loadSettings();

    QString getClipboardContent(bool force = false);
    void setClipboardContent(const QString &content);

    bool isOwnClipboardChange(const QString &content);
    void markAsOwnContent(const QString &content);
    QString generateContentHash(const QString &content);

    bool sendClipboardToServer(const QString &content);
    QString getClipboardFromServer();

    static constexpr int DEFAULT_MAX_SIZE = 1048576; // 1 MB
    static const QString CLIPBOARD_IDENTIFIER;

    QClipboard *m_clipboard;

    NvComputer *m_computer;
    NvHTTP *m_http;

    bool m_smartSyncEnabled;
    bool m_bidirectionalSync;
    bool m_showToast;
    bool m_hideContent;
    int m_maxClipboardSize;

    bool m_enabled;
    bool m_connected;
    bool m_textOnlyMode;
    int m_maxContentSizeMB;
    bool m_showNotifications;

    QString m_lastSentContent;
    QString m_lastReceivedContent;
    QStringList m_ownContentHashes;
    bool m_syncInProgress;

    static ClipboardManager* s_instance;
};

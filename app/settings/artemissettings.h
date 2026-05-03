// Ported from wjbeckett/artemis (GPL). Slimmed to clipboard-sync settings only;
// other artemis features (server commands, OTP pairing, virtual display, etc.)
// are not part of this fork.
#pragma once

#include <QObject>
#include <QSettings>
#include <QQmlEngine>

class ArtemisSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool clipboardSyncEnabled READ clipboardSyncEnabled WRITE setClipboardSyncEnabled NOTIFY clipboardSyncEnabledChanged)
    Q_PROPERTY(bool clipboardSyncBidirectional READ clipboardSyncBidirectional WRITE setClipboardSyncBidirectional NOTIFY clipboardSyncBidirectionalChanged)
    Q_PROPERTY(int clipboardSyncMaxSize READ clipboardSyncMaxSize WRITE setClipboardSyncMaxSize NOTIFY clipboardSyncMaxSizeChanged)

public:
    static ArtemisSettings* instance();
    static QObject* qmlInstance(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    Q_INVOKABLE void save();
    Q_INVOKABLE void load();
    Q_INVOKABLE void resetToDefaults();

    bool clipboardSyncEnabled() const { return m_clipboardSyncEnabled; }
    void setClipboardSyncEnabled(bool enabled);

    bool clipboardSyncBidirectional() const { return m_clipboardSyncBidirectional; }
    void setClipboardSyncBidirectional(bool bidirectional);

    int clipboardSyncMaxSize() const { return m_clipboardSyncMaxSize; }
    void setClipboardSyncMaxSize(int maxSize);

signals:
    void clipboardSyncEnabledChanged();
    void clipboardSyncBidirectionalChanged();
    void clipboardSyncMaxSizeChanged();

private:
    explicit ArtemisSettings(QObject *parent = nullptr);

    void loadDefaults();

    static ArtemisSettings* s_instance;
    QSettings *m_settings;

    bool m_clipboardSyncEnabled;
    bool m_clipboardSyncBidirectional;
    int m_clipboardSyncMaxSize;
};

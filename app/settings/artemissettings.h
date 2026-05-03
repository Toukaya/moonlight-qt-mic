// Ported from wjbeckett/artemis (GPL). Slimmed to clipboard-sync settings only;
// other artemis features (server commands, OTP pairing, virtual display, etc.)
// are not part of this fork.
//
// Pattern matches StreamingPreferences: public MEMBER properties with a single
// explicit save() called from SettingsView.qml lifecycle hooks. There are no
// per-field setters; QML writes the property directly via the auto-generated
// MEMBER setter, which fires the NOTIFY signal automatically.
#pragma once

#include <QObject>
#include <QSettings>
#include <QQmlEngine>

class ArtemisSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool clipboardSyncEnabled MEMBER clipboardSyncEnabled NOTIFY clipboardSyncEnabledChanged)
    Q_PROPERTY(bool clipboardSyncBidirectional MEMBER clipboardSyncBidirectional NOTIFY clipboardSyncBidirectionalChanged)
    Q_PROPERTY(int clipboardSyncMaxSize MEMBER clipboardSyncMaxSize NOTIFY clipboardSyncMaxSizeChanged)
    Q_PROPERTY(bool clipboardSyncTextOnly MEMBER clipboardSyncTextOnly NOTIFY clipboardSyncTextOnlyChanged)
    Q_PROPERTY(bool clipboardSyncShowNotifications MEMBER clipboardSyncShowNotifications NOTIFY clipboardSyncShowNotificationsChanged)

public:
    static ArtemisSettings* instance();
    static QObject* qmlInstance(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    Q_INVOKABLE void save();
    Q_INVOKABLE void load();
    Q_INVOKABLE void resetToDefaults();

    // Public members exposed via MEMBER Q_PROPERTY (Moonlight convention).
    bool clipboardSyncEnabled;
    bool clipboardSyncBidirectional;
    int clipboardSyncMaxSize;
    bool clipboardSyncTextOnly;
    bool clipboardSyncShowNotifications;

signals:
    void clipboardSyncEnabledChanged();
    void clipboardSyncBidirectionalChanged();
    void clipboardSyncMaxSizeChanged();
    void clipboardSyncTextOnlyChanged();
    void clipboardSyncShowNotificationsChanged();

private:
    explicit ArtemisSettings(QObject *parent = nullptr);

    void loadDefaults();

    static ArtemisSettings* s_instance;
    QSettings *m_settings;
};

// Ported from wjbeckett/artemis (GPL). Clipboard-sync settings only.
//
// MEMBER-style (Moonlight convention): public fields, no setters. QML writes
// fields directly; persistence is done by the explicit save() call from
// SettingsView.qml lifecycle hooks (onDeactivating / Component.onDestruction).
#include "artemissettings.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

ArtemisSettings* ArtemisSettings::s_instance = nullptr;

ArtemisSettings::ArtemisSettings(QObject *parent)
    : QObject(parent)
    , clipboardSyncEnabled(false)
    , clipboardSyncBidirectional(true)
    , clipboardSyncMaxSize(1048576)
    , clipboardSyncTextOnly(true)
    , clipboardSyncShowNotifications(true)
    , m_settings(nullptr)
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir configDir(configPath);
    if (!configDir.exists()) {
        configDir.mkpath(".");
    }

    QString settingsPath = configPath + "/artemis-settings.ini";
    m_settings = new QSettings(settingsPath, QSettings::IniFormat, this);

    loadDefaults();
    load();

    qDebug() << "ArtemisSettings: Initialized with config at" << settingsPath;
}

ArtemisSettings* ArtemisSettings::instance()
{
    if (!s_instance) {
        s_instance = new ArtemisSettings();
    }
    return s_instance;
}

QObject* ArtemisSettings::qmlInstance(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    return instance();
}

void ArtemisSettings::save()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup("ClipboardSync");
    m_settings->setValue("enabled", clipboardSyncEnabled);
    m_settings->setValue("bidirectional", clipboardSyncBidirectional);
    m_settings->setValue("maxSize", clipboardSyncMaxSize);
    m_settings->setValue("textOnly", clipboardSyncTextOnly);
    m_settings->setValue("showNotifications", clipboardSyncShowNotifications);
    m_settings->endGroup();

    m_settings->sync();
}

void ArtemisSettings::load()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup("ClipboardSync");
    clipboardSyncEnabled = m_settings->value("enabled", false).toBool();
    clipboardSyncBidirectional = m_settings->value("bidirectional", true).toBool();
    clipboardSyncMaxSize = m_settings->value("maxSize", 1048576).toInt();
    clipboardSyncTextOnly = m_settings->value("textOnly", true).toBool();
    clipboardSyncShowNotifications = m_settings->value("showNotifications", true).toBool();
    m_settings->endGroup();
}

void ArtemisSettings::resetToDefaults()
{
    loadDefaults();
    save();

    emit clipboardSyncEnabledChanged();
    emit clipboardSyncBidirectionalChanged();
    emit clipboardSyncMaxSizeChanged();
    emit clipboardSyncTextOnlyChanged();
    emit clipboardSyncShowNotificationsChanged();
}

void ArtemisSettings::loadDefaults()
{
    clipboardSyncEnabled = false;
    clipboardSyncBidirectional = true;
    clipboardSyncMaxSize = 1048576;
    clipboardSyncTextOnly = true;
    clipboardSyncShowNotifications = true;
}

// Ported from wjbeckett/artemis (GPL). Clipboard-sync settings only.
#include "artemissettings.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

ArtemisSettings* ArtemisSettings::s_instance = nullptr;

ArtemisSettings::ArtemisSettings(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr)
    , m_clipboardSyncEnabled(false)
    , m_clipboardSyncBidirectional(true)
    , m_clipboardSyncMaxSize(1048576)
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
    m_settings->setValue("enabled", m_clipboardSyncEnabled);
    m_settings->setValue("bidirectional", m_clipboardSyncBidirectional);
    m_settings->setValue("maxSize", m_clipboardSyncMaxSize);
    m_settings->endGroup();

    m_settings->sync();
}

void ArtemisSettings::load()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup("ClipboardSync");
    m_clipboardSyncEnabled = m_settings->value("enabled", false).toBool();
    m_clipboardSyncBidirectional = m_settings->value("bidirectional", true).toBool();
    m_clipboardSyncMaxSize = m_settings->value("maxSize", 1048576).toInt();
    m_settings->endGroup();
}

void ArtemisSettings::resetToDefaults()
{
    loadDefaults();
    save();

    emit clipboardSyncEnabledChanged();
    emit clipboardSyncBidirectionalChanged();
    emit clipboardSyncMaxSizeChanged();
}

void ArtemisSettings::loadDefaults()
{
    m_clipboardSyncEnabled = false;
    m_clipboardSyncBidirectional = true;
    m_clipboardSyncMaxSize = 1048576;
}

void ArtemisSettings::setClipboardSyncEnabled(bool enabled)
{
    if (m_clipboardSyncEnabled != enabled) {
        m_clipboardSyncEnabled = enabled;
        emit clipboardSyncEnabledChanged();
    }
}

void ArtemisSettings::setClipboardSyncBidirectional(bool bidirectional)
{
    if (m_clipboardSyncBidirectional != bidirectional) {
        m_clipboardSyncBidirectional = bidirectional;
        emit clipboardSyncBidirectionalChanged();
    }
}

void ArtemisSettings::setClipboardSyncMaxSize(int maxSize)
{
    if (m_clipboardSyncMaxSize != maxSize) {
        m_clipboardSyncMaxSize = maxSize;
        emit clipboardSyncMaxSizeChanged();
    }
}

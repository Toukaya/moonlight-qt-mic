// Ported from wjbeckett/artemis (GPL). ServerCommandManager dependency
// stripped — logabell does not have that subsystem; the menu only exposes
// disconnect / quit / clipboard / stats / fullscreen actions.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QWindow>
#include <QQuickItem>
#include <QQuickView>

class ClipboardManager;

class QuickMenuManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isVisible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool isFullscreen READ isFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(bool isMouseCaptured READ isMouseCaptured NOTIFY mouseCaptureChanged)
    Q_PROPERTY(bool isKeyboardCaptured READ isKeyboardCaptured NOTIFY keyboardCaptureChanged)
    Q_PROPERTY(bool isStatsVisible READ isStatsVisible NOTIFY statsVisibilityChanged)

public:
    explicit QuickMenuManager(QObject *parent = nullptr);
    ~QuickMenuManager();

    bool isVisible() const { return m_isVisible; }
    bool isFullscreen() const;
    bool isMouseCaptured() const;
    bool isKeyboardCaptured() const;
    bool isStatsVisible() const;

    Q_INVOKABLE void setVisible(bool visible);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();

    Q_INVOKABLE void executeAction(const QString &action);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void quit();
    Q_INVOKABLE void uploadClipboard();
    Q_INVOKABLE void fetchClipboard();
    Q_INVOKABLE void toggleStats();
    Q_INVOKABLE void toggleMouseCapture();
    Q_INVOKABLE void toggleKeyboardCapture();
    Q_INVOKABLE void toggleFullscreen();

    void setClipboardManager(ClipboardManager *manager);

    void setWindow(QWindow *window);
    void setWindowGeometry(int x, int y, int width, int height);

signals:
    void visibilityChanged();
    void fullscreenChanged();
    void mouseCaptureChanged();
    void keyboardCaptureChanged();
    void statsVisibilityChanged();

    void disconnectRequested();
    void quitRequested();
    void clipboardUploadRequested();
    void clipboardFetchRequested();
    void statsToggleRequested();
    void mouseCaptureToggleRequested();
    void keyboardCaptureToggleRequested();
    void fullscreenToggleRequested();

private:
    void createQuickView();
    void sendSdlKey(int sdlScancode, int sdlKeycode, quint16 modifiers);

    bool m_isVisible;
    QWindow *m_window;
    QQuickView *m_quickView;
    QQuickItem *m_quickMenuItem;

    ClipboardManager *m_clipboardManager;

    bool m_isFullscreen;
    bool m_isMouseCaptured;
    bool m_isKeyboardCaptured;
    bool m_isStatsVisible;

    int m_windowX, m_windowY, m_windowWidth, m_windowHeight;
    bool m_hasWindowGeometry;
};

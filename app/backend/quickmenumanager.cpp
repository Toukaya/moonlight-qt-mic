// Ported from wjbeckett/artemis (GPL). ServerCommandManager dependency
// stripped; toggle{Mouse,Keyboard,Fullscreen} use a real SDL key-injection
// path through the existing sendSdlKey helper instead of the placeholder
// stub that artemis shipped.
#include "quickmenumanager.h"
#include "clipboardmanager.h"
#include "../streaming/session.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QWindow>
#include <QUrl>
#include <QDebug>
#include <QTimer>

#include <SDL.h>

QuickMenuManager::QuickMenuManager(QObject *parent)
    : QObject(parent)
    , m_isVisible(false)
    , m_window(nullptr)
    , m_quickView(nullptr)
    , m_quickMenuItem(nullptr)
    , m_clipboardManager(nullptr)
    , m_isFullscreen(false)
    , m_isMouseCaptured(false)
    , m_isKeyboardCaptured(false)
    , m_isStatsVisible(false)
    , m_windowX(0)
    , m_windowY(0)
    , m_windowWidth(800)
    , m_windowHeight(600)
    , m_hasWindowGeometry(false)
{
}

QuickMenuManager::~QuickMenuManager()
{
    if (m_quickView) {
        delete m_quickView;
    }
}

bool QuickMenuManager::isFullscreen() const
{
    return m_isFullscreen;
}

bool QuickMenuManager::isMouseCaptured() const
{
    return m_isMouseCaptured;
}

bool QuickMenuManager::isKeyboardCaptured() const
{
    return m_isKeyboardCaptured;
}

bool QuickMenuManager::isStatsVisible() const
{
    return m_isStatsVisible;
}

void QuickMenuManager::setVisible(bool visible)
{
    if (m_isVisible == visible) {
        return;
    }

    m_isVisible = visible;

    if (visible) {
        createQuickView();
    } else {
        if (m_quickView) {
            m_quickView->hide();
        }
    }

    emit visibilityChanged();
}

void QuickMenuManager::toggle()
{
    setVisible(!m_isVisible);
}

void QuickMenuManager::show()
{
    setVisible(true);
}

void QuickMenuManager::hide()
{
    setVisible(false);
}

void QuickMenuManager::executeAction(const QString &action)
{
    qDebug() << "QuickMenuManager: Executing action:" << action;

    if (action == "disconnect") {
        disconnect();
    } else if (action == "quit") {
        quit();
    } else if (action == "clipboard_upload") {
        uploadClipboard();
    } else if (action == "clipboard_fetch") {
        fetchClipboard();
    } else if (action == "toggle_stats") {
        toggleStats();
    } else if (action == "toggle_mouse") {
        toggleMouseCapture();
    } else if (action == "toggle_keyboard") {
        toggleKeyboardCapture();
    } else if (action == "toggle_fullscreen") {
        toggleFullscreen();
    }
}

void QuickMenuManager::disconnect()
{
    emit disconnectRequested();

    SDL_Event quitEvent;
    quitEvent.type = SDL_QUIT;
    quitEvent.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&quitEvent);
}

void QuickMenuManager::quit()
{
    emit quitRequested();

    if (Session::get()) {
        // Logabell's setShouldExit(true) maps to artemis's setShouldExitAfterQuit():
        // true forces the host-app quit; the quit happens after the SDL_QUIT below.
        Session::get()->setShouldExit(true);
    }

    SDL_Event quitEvent;
    quitEvent.type = SDL_QUIT;
    quitEvent.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&quitEvent);
}

void QuickMenuManager::uploadClipboard()
{
    emit clipboardUploadRequested();

    if (m_clipboardManager) {
        m_clipboardManager->sendClipboard(true);
    }
}

void QuickMenuManager::fetchClipboard()
{
    emit clipboardFetchRequested();

    if (m_clipboardManager) {
        m_clipboardManager->getClipboard();
    }
}

void QuickMenuManager::toggleStats()
{
    emit statsToggleRequested();

    if (Session::get()) {
        auto& overlayManager = Session::get()->getOverlayManager();
        bool currentState = overlayManager.isOverlayEnabled(Overlay::OverlayDebug);
        overlayManager.setOverlayState(Overlay::OverlayDebug, !currentState);

        m_isStatsVisible = !currentState;
        emit statsVisibilityChanged();
    }
}

void QuickMenuManager::toggleMouseCapture()
{
    emit mouseCaptureToggleRequested();
    // Re-fire the existing Ctrl+Alt+Shift+M combo so SdlInputHandler runs its full toggle path.
    sendSdlKey(SDL_SCANCODE_M, SDLK_m, KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT);
}

void QuickMenuManager::toggleKeyboardCapture()
{
    emit keyboardCaptureToggleRequested();
    // Logabell does not have an explicit keyboard-capture toggle combo;
    // ungrab-input (Ctrl+Alt+Shift+Z) is the closest equivalent.
    sendSdlKey(SDL_SCANCODE_Z, SDLK_z, KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT);
}

void QuickMenuManager::toggleFullscreen()
{
    emit fullscreenToggleRequested();
    sendSdlKey(SDL_SCANCODE_X, SDLK_x, KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT);
}

void QuickMenuManager::setClipboardManager(ClipboardManager *manager)
{
    m_clipboardManager = manager;
}

void QuickMenuManager::setWindow(QWindow *window)
{
    m_window = window;
}

void QuickMenuManager::setWindowGeometry(int x, int y, int width, int height)
{
    m_windowX = x;
    m_windowY = y;
    m_windowWidth = width;
    m_windowHeight = height;
    m_hasWindowGeometry = true;
}

void QuickMenuManager::createQuickView()
{
    if (!m_quickView) {
        m_quickView = new QQuickView();
        m_quickView->setResizeMode(QQuickView::SizeViewToRootObject);

        QQmlContext *context = m_quickView->rootContext();
        context->setContextProperty("quickMenuManager", this);

        m_quickView->setSource(QUrl("qrc:/gui/QuickMenu.qml"));

        if (m_quickView->status() == QQuickView::Error) {
            qWarning() << "QuickMenuManager: Error loading QML:" << m_quickView->errors();
            return;
        }

        m_quickMenuItem = m_quickView->rootObject();
        if (m_quickMenuItem) {
            m_quickMenuItem->setProperty("visible", true);
        }

        m_quickView->setFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
        m_quickView->setColor(QColor(Qt::transparent));

        int centerX, centerY;
        if (m_hasWindowGeometry) {
            centerX = m_windowX + (m_windowWidth - 500) / 2;
            centerY = m_windowY + (m_windowHeight - 400) / 2;
            m_quickView->setGeometry(centerX, centerY, 500, 400);
        } else {
            m_quickView->setGeometry(400, 300, 500, 400);
        }
    }

    if (m_quickView) {
        m_quickView->show();
        m_quickView->raise();
        m_quickView->requestActivate();
    }
}

void QuickMenuManager::sendSdlKey(int sdlScancode, int sdlKeycode, quint16 modifiers)
{
    SDL_Event keyDown;
    SDL_zero(keyDown);
    keyDown.type = SDL_KEYDOWN;
    keyDown.key.timestamp = SDL_GetTicks();
    keyDown.key.state = SDL_PRESSED;
    keyDown.key.keysym.scancode = (SDL_Scancode)sdlScancode;
    keyDown.key.keysym.sym = (SDL_Keycode)sdlKeycode;
    keyDown.key.keysym.mod = modifiers;
    SDL_PushEvent(&keyDown);

    SDL_Event keyUp = keyDown;
    keyUp.type = SDL_KEYUP;
    keyUp.key.state = SDL_RELEASED;
    SDL_PushEvent(&keyUp);
}

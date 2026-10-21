/**
 * @file app/streaming/seamless/seamlesswindow.mm
 * @brief One local window per remote window: its frame, its drawable and the
 *        canvas rectangle it samples.
 */
#include "seamlesswindow.h"

#import <AppKit/AppKit.h>
#include <SDL_log.h>

#include "seamlessinstruments.h"

#include <QMutex>
#include <QMutexLocker>

#include <cmath>

// The view inside a seamless window: it takes focus and forwards input to the
// host, the same way the stream view does today for the desktop.
@interface SeamlessInputView : NSView
@property(nonatomic) quint32 windowId;
// The window's index in the applied table: unique (the table caps at 32) and it
// fits the one-byte id field of the input and intent messages.
@property(nonatomic) uint8_t tableIndex;
// Keys this view sent as text, so their up event is not sent as a virtual key:
// the text event is complete in itself and an orphan key-up is not.
@property(nonatomic, strong) NSMutableSet<NSNumber*>* textKeys;
// Where this window sits on the host desktop, and how large it is there: the
// mouse the client sends is a DESKTOP position, not a window fraction.
@property(nonatomic) NSPoint cellPos;
@property(nonatomic) NSSize clientSize;
@property(nonatomic) NSSize canvasSize;
@end

@implementation SeamlessInputView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)needsPanelToBecomeKey { return NO; }

- (BOOL)acceptsFirstMouse:(NSEvent*)event { Q_UNUSED(event); return YES; }

- (void)focusHost:(uint8_t)mode
{
    // 0x52 WindowInput: the client tells the host which window is focused, so the
    // host can raise it before the next input lands.
    uint8_t message[SEAMLESS_INPUT_SIZE] {};
    message[0] = SEAMLESS_MSG_WINDOW_INPUT;
    message[1] = self.tableIndex;
    message[2] = mode;
    LiSendSeamlessMessage(message, (int) sizeof(message));
}

- (void)mouseDown:(NSEvent*)event { [self focusHost:SEAMLESS_INPUT_FOCUS_RAISE]; LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT); }
- (void)mouseUp:(NSEvent*)event { LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT); }
- (void)rightMouseDown:(NSEvent*)event { [self focusHost:SEAMLESS_INPUT_FOCUS_RAISE]; LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT); }
- (void)rightMouseUp:(NSEvent*)event { LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT); }

- (void)mouseMoved:(NSEvent*)event { [self moveHost:event]; }
- (void)mouseDragged:(NSEvent*)event { [self moveHost:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self moveHost:event]; }

- (void)moveHost:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSRect bounds = self.bounds;
    if (bounds.size.width <= 0 || bounds.size.height <= 0 ||
        self.clientSize.width <= 0 || self.clientSize.height <= 0 ||
        self.canvasSize.width <= 0 || self.canvasSize.height <= 0) {
        return;
    }

    // The position is in CANVAS space, which is the space the reference size names:
    // the cell's canvas origin plus that fraction of the cell. The host rescales
    // canvas to desktop (desktopW / referenceW), so the two agree; sending desktop
    // pixels against a canvas reference is what double-scaled it.
    const double fx = qBound(0.0, point.x / bounds.size.width, 1.0);
    const double fy = qBound(0.0, 1.0 - point.y / bounds.size.height, 1.0);
    const double canvasX = self.cellPos.x + fx * self.clientSize.width;
    const double canvasY = self.cellPos.y + fy * self.clientSize.height;

    LiSendMousePositionEvent((short) qBound(-32768.0, canvasX, 32767.0),
                             (short) qBound(-32768.0, canvasY, 32767.0),
                             (short) self.canvasSize.width, (short) self.canvasSize.height);
}

/// The host's virtual key for a macOS key event: a character for letters and
/// digits (VK_A..VK_Z, VK_0..VK_9 are their ASCII codes), a table for the rest.
static short host_vk_for_event(NSEvent* event)
{
    switch (event.keyCode) {
    case 36: return 0x0D;  // return
    case 48: return 0x09;  // tab
    case 49: return 0x20;  // space
    case 51: return 0x08;  // delete (backspace)
    case 53: return 0x1B;  // escape
    case 76: return 0x0D;  // keypad enter
    case 117: return 0x2E;  // forward delete
    case 123: return 0x25;  // left
    case 124: return 0x27;  // right
    case 125: return 0x28;  // down
    case 126: return 0x26;  // up
    case 115: return 0x24;  // home
    case 119: return 0x23;  // end
    case 116: return 0x21;  // page up
    case 121: return 0x22;  // page down
    default: break;
    }

    NSString* characters = event.charactersIgnoringModifiers;
    if (characters.length == 0) {
        return 0;
    }
    const unichar c = [characters characterAtIndex:0];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return (short) QChar(c).toUpper().unicode();
    }
    if (c >= '0' && c <= '9') {
        return (short) c;
    }
    return 0;
}

static short host_modifiers_for_event(NSEvent* event)
{
    short modifiers = 0;
    const NSEventModifierFlags flags = event.modifierFlags;
    if (flags & NSEventModifierFlagShift) modifiers |= MODIFIER_SHIFT;
    if (flags & NSEventModifierFlagControl) modifiers |= MODIFIER_CTRL;
    if (flags & NSEventModifierFlagOption) modifiers |= MODIFIER_ALT;
    if (flags & NSEventModifierFlagCommand) modifiers |= MODIFIER_META;
    return modifiers;
}

- (void)scrollWheel:(NSEvent*)event
{
    if (event.hasPreciseScrollingDeltas) {
        LiSendHighResScrollEvent((short) qBound(-32767.0, event.scrollingDeltaY, 32767.0));
    }
    else {
        LiSendScrollEvent((signed char) qBound(-128.0, event.scrollingDeltaY, 127.0));
    }
}

- (void)keyDown:(NSEvent*)event
{
    [self focusHost:SEAMLESS_INPUT_FOCUS];

    // Printable text goes up as UTF-8: the host types the characters it receives,
    // so punctuation, symbols and non-ASCII text are not lost to a key table. Only
    // the keys that are not text (arrows, enter, tab, escape, delete, home/end/page)
    // travel as virtual-key events.
    const NSEventModifierFlags flags = event.modifierFlags;
    NSString* characters = event.characters;
    if (characters.length > 0 && (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) == 0) {
        // Only PRINTABLE text goes up as UTF-8: the control characters (return,
        // tab, escape, delete) carry characters too, and they belong to the
        // virtual-key path below, not to the text path.
        bool printable = false;
        for (NSUInteger i = 0; i < characters.length; i++) {
            const unichar c = [characters characterAtIndex:i];
            if (c >= 0x20 && c != 0x7F) {
                printable = true;
                break;
            }
        }
        if (printable) {
            LiSendUtf8TextEvent(characters.UTF8String, (int) strlen(characters.UTF8String));
            // Remember the key so its up event is not sent as a virtual key.
            if (self.textKeys == nil) {
                self.textKeys = [NSMutableSet set];
            }
            [self.textKeys addObject:@(event.keyCode)];
            return;
        }
    }

    const short vk = host_vk_for_event(event);
    if (vk != 0) {
        LiSendKeyboardEvent(vk, KEY_ACTION_DOWN, host_modifiers_for_event(event));
    }
}

- (void)keyUp:(NSEvent*)event
{
    // Per-key symmetry: a key that went up as text gets no virtual-key up.
    if ([self.textKeys containsObject:@(event.keyCode)]) {
        [self.textKeys removeObject:@(event.keyCode)];
        return;
    }

    const short vk = host_vk_for_event(event);
    if (vk != 0) {
        LiSendKeyboardEvent(vk, KEY_ACTION_UP, host_modifiers_for_event(event));
    }
}

@end

@interface SeamlessWindow () <NSWindowDelegate>
@end

@implementation SeamlessWindow {
    NSWindow *_window;
    NSView *_view;
    CAMetalLayer *_layer;
    id<MTLDevice> _device;
    quint32 _windowId;
    CGSize _canvasSize;
    simd_float4 _cellUv;
    // The render thread writes the newest entry, the main thread applies it, and
    // the render thread reads the sampling rect from the same slot - one struct,
    // one lock, so a frame can never pair a new cell with an old visibility.
    QMutex _pendingLock;
    SeamlessModel::WindowEntry _pendingEntry;
    CGSize _pendingCanvas;
    bool _pendingClose;
    bool _hasPending;
    // What the render thread samples: written under the lock, used by the quad.
    simd_float4 _renderCellUv;
    bool _renderVisible;
    CGSize _renderDrawableSize;
}

- (instancetype)initWithWindowId:(quint32)windowId device:(id<MTLDevice>)device
{
    self = [super init];
    if (self == nil) {
        return nil;
    }

    _windowId = windowId;
    _device = device;
    _canvasSize = CGSizeMake(1, 1);
    _cellUv = simd_make_float4(0, 0, 1, 1);
    _pendingCanvas = CGSizeMake(1, 1);
    _pendingClose = false;
    _hasPending = false;
    _renderCellUv = simd_make_float4(0, 0, 1, 1);
    _renderVisible = false;
    _renderDrawableSize = CGSizeMake(0, 0);

    // An ordinary window: the remote application looks like a local one, and the
    // user moves and stacks it with the window server, not inside a stream view.
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskResizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    if (_window == nil) {
        return nil;
    }

    _view = [[SeamlessInputView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    _layer = [CAMetalLayer layer];
    _layer.device = _device;
    _layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _layer.framebufferOnly = YES;
    _layer.opaque = YES;
    ((SeamlessInputView *) _view).windowId = windowId;
    _view.layer = _layer;
    _view.wantsLayer = YES;
    _window.contentView = _view;
    _window.delegate = self;
    _window.releasedWhenClosed = NO;

    return self;
}

- (void)updateWithEntry:(const SeamlessModel::WindowEntry &)entry
             canvasSize:(CGSize)canvasSize
{
    bool dispatch = false;
    {
        QMutexLocker locker(&_pendingLock);

        // The sampling rect and the visibility are what the render thread needs,
        // and they are cheap to keep current on every version.
        const CGFloat canvasW = MAX((CGFloat) canvasSize.width, 1.0);
        const CGFloat canvasH = MAX((CGFloat) canvasSize.height, 1.0);
        _renderCellUv = simd_make_float4((float) (entry.cellPos.x() / canvasW),
                                         (float) (entry.cellPos.y() / canvasH),
                                         (float) (entry.cellSize.width() / canvasW),
                                         (float) (entry.cellSize.height() / canvasH));
        _renderVisible = (entry.flags & SEAMLESS_WF_VISIBLE) != 0;

        // One main-thread block per window per *change*, not per frame.
        if (!_hasPending || _pendingEntry.cellPos != entry.cellPos ||
            _pendingEntry.cellSize != entry.cellSize || _pendingEntry.clientSize != entry.clientSize ||
            _pendingEntry.flags != entry.flags || _pendingCanvas.width != canvasSize.width ||
            _pendingCanvas.height != canvasSize.height) {
            _pendingEntry = entry;
            _pendingCanvas = canvasSize;
            _hasPending = true;
            dispatch = true;
        }
    }

    if (!dispatch) {
        return;
    }

    // The window server belongs to the main thread; this may be the render
    // thread, so the AppKit part is handed over rather than performed here.
    [self performSelectorOnMainThread:@selector(applyPendingGeometry)
                           withObject:nil
                        waitUntilDone:NO];
}

- (void)applyPendingGeometry
{
    // C-3: this is the feature's main-thread cost, and it is measured here, in
    // the block that actually runs on the main thread - the display-link timer
    // cannot see it.
    const auto startedAt = std::chrono::steady_clock::now();
    struct ApplyDone {
        SeamlessWindow *self;
        std::chrono::steady_clock::time_point started;
        ~ApplyDone() {
            const double us = std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - started).count();
            [self noteApplyUs:us];
        }
    } applyDone {self, startedAt};

    SeamlessModel::WindowEntry entry;
    CGSize canvasSize;
    {
        QMutexLocker locker(&_pendingLock);
        if (!_hasPending) {
            return;
        }
        entry = _pendingEntry;
        canvasSize = _pendingCanvas;
        _hasPending = false;
    }

    _canvasSize = canvasSize;

    // The drawable is the local pixel size: the remote content size times the
    // scale of the screen the window is on. A Retina screen therefore renders at
    // 2x, and the sampling stays in canvas coordinates, so the content is not
    // resampled just because the display is denser.
    CGFloat scale = _window.screen.backingScaleFactor;
    if (scale <= 0) {
        scale = 1.0;
    }

    CGSize content = CGSizeMake(entry.clientSize.width(), entry.clientSize.height());
    NSSize frameSize = [_window frameRectForContentRect:NSMakeRect(0, 0, content.width, content.height)].size;

    // Never bigger than the screen it opens on, and never with two factors: one
    // factor for both axes keeps the aspect, so a clamped window is smaller, not
    // stretched.
    NSRect visible = _window.screen.visibleFrame;
    if (visible.size.width > 0 && visible.size.height > 0) {
        CGFloat factor = 1.0;
        if (frameSize.width > visible.size.width) {
            factor = MIN(factor, visible.size.width / frameSize.width);
        }
        if (frameSize.height > visible.size.height) {
            factor = MIN(factor, visible.size.height / frameSize.height);
        }
        frameSize.width *= factor;
        frameSize.height *= factor;
    }

    NSRect frame = _window.frame;
    frame.size = frameSize;
    [_window setFrame:frame display:NO];
    _layer.drawableSize = CGSizeMake(content.width * scale, content.height * scale);
    {
        // The render thread reads the size it will draw into; it must see the
        // same value as the layer, under the same lock as the cell.
        QMutexLocker locker(&_pendingLock);
        _renderDrawableSize = _layer.drawableSize;
    }
    seamless_instruments::drawableW.store((int) (_layer.drawableSize.width + 0.5));
    seamless_instruments::drawableH.store((int) (_layer.drawableSize.height + 0.5));
    seamless_instruments::drawableScaleX100.store((int) (scale * 100 + 0.5));

    // The cell is the window's whole rectangle on the canvas (the host never
    // crops a cell), so this rectangle and the drawable describe the same
    // content and nothing is stretched.
    const CGFloat canvasW = MAX((CGFloat) canvasSize.width, 1.0);
    const CGFloat canvasH = MAX((CGFloat) canvasSize.height, 1.0);
    _cellUv = simd_make_float4((float) (entry.cellPos.x() / canvasW),
                               (float) (entry.cellPos.y() / canvasH),
                               (float) (entry.cellSize.width() / canvasW),
                               (float) (entry.cellSize.height() / canvasH));

    // A minimized remote window keeps its local window, hidden rather than
    // destroyed, so restoring it does not re-create the window server object.
    // The measurement instrument for the Retina line: the content size, the
    // backing scale it was opened at, and the drawable size that follows.
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Seamless: window %u content %gx%g scale %.2f drawable %.0fx%.0f cell %d,%d %dx%d on canvas %gx%g",
                (unsigned) _windowId, (double) content.width, (double) content.height, (double) scale,
                (double) _layer.drawableSize.width, (double) _layer.drawableSize.height,
                entry.cellPos.x(), entry.cellPos.y(), entry.cellSize.width(), entry.cellSize.height(),
                (double) canvasSize.width, (double) canvasSize.height);

    // QA's line-4 verdict: the drawable must be the content size times the
    // backing scale within half a pixel, and the cell must be the client size.
    {
        const double expectedW = content.width * scale;
        const double expectedH = content.height * scale;
        const bool drawableOk = std::abs(_layer.drawableSize.width - expectedW) <= 0.5 &&
                                std::abs(_layer.drawableSize.height - expectedH) <= 0.5;
        const bool cellOk = entry.cellSize.width() == entry.clientSize.width() &&
                            entry.cellSize.height() == entry.clientSize.height();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Seamless: scale verdict window=%u scale=%.2f drawable_ok=%d cell_ok=%d",
                    (unsigned) _windowId, (double) scale, drawableOk ? 1 : 0, cellOk ? 1 : 0);
    }

    if ([_view isKindOfClass:[SeamlessInputView class]]) {
        SeamlessInputView* inputView = (SeamlessInputView*) _view;
        inputView.cellPos = NSPointFromCGPoint(CGPointMake(entry.cellPos.x(), entry.cellPos.y()));
        inputView.clientSize = NSSizeFromCGSize(CGSizeMake(entry.clientSize.width(), entry.clientSize.height()));
        inputView.canvasSize = NSSizeFromCGSize(canvasSize);
    }

    _layer.hidden = (entry.flags & SEAMLESS_WF_VISIBLE) == 0;
    if (!_layer.hidden && !_window.isVisible) {
        [_window orderFrontRegardless];
    }
    if (!_layer.hidden) {
        [_window makeFirstResponder:_view];
    }
}

- (id<CAMetalDrawable>)nextDrawable
{
    // Under backpressure the drawable call blocks; the lock must not be held
    // across it or the main thread waits inside applyPendingGeometry.
    CGSize drawableSize;
    {
        QMutexLocker locker(&_pendingLock);
        if (!_renderVisible) {
            return nil;
        }
        drawableSize = _renderDrawableSize;
    }
    if (drawableSize.width < 1 || drawableSize.height < 1) {
        return nil;
    }
    return [_layer nextDrawable];
}

- (void)copyCellUvTo:(simd_float4 *)out
{
    QMutexLocker locker(&_pendingLock);
    *out = _renderCellUv;
}

- (quint32)windowId
{
    return _windowId;
}

- (void)noteApplyUs:(double)us
{
    static double sum;
    static double max;
    static int count;
    sum += us;
    max = MAX(max, us);
    seamless_instruments::mainApplyUs.store((int) (us + 0.5));
    count++;
    if (count % 120 == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Seamless: main apply avg=%.1f us max=%.1f us over %d applies",
                    sum / count, max, count);
    }
}

- (void)windowWillClose:(NSNotification*)notification
{
    // Kind 5 of the existing intent message: closing the local window closes the
    // window it belongs to on the host. The ruling added it as a VALUE of the
    // existing kind field, so the wire layout is untouched.
    uint8_t message[SEAMLESS_INTENT_SIZE] {};
    message[0] = SEAMLESS_MSG_WINDOW_INTENT;
    // The index into the applied table, exactly as the focus message sends it:
    // the host resolves this byte as an index, not as a handle.
    message[1] = self.tableIndex;
    message[2] = SEAMLESS_INTENT_CLOSE;
    LiSendSeamlessMessage(message, (int) sizeof(message));
    Q_UNUSED(notification);
}

- (void)close
{
    [self performSelectorOnMainThread:@selector(closeOnMain)
                           withObject:nil
                        waitUntilDone:NO];
}

- (void)closeOnMain
{
    [_window orderOut:nil];
    _layer.hidden = YES;
}

@end

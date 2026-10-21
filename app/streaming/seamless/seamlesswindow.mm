/**
 * @file app/streaming/seamless/seamlesswindow.mm
 * @brief One local window per remote window: its frame, its drawable and the
 *        canvas rectangle it samples.
 */
#include "seamlesswindow.h"

#import <AppKit/AppKit.h>
#include <SDL_log.h>

#include <QMutex>
#include <QMutexLocker>

#include <cmath>

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

    _view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    _layer = [CAMetalLayer layer];
    _layer.device = _device;
    _layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _layer.framebufferOnly = YES;
    _layer.opaque = YES;
    _view.layer = _layer;
    _view.wantsLayer = YES;
    _window.contentView = _view;
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

    _layer.hidden = (entry.flags & SEAMLESS_WF_VISIBLE) == 0;
    if (!_layer.hidden && !_window.isVisible) {
        [_window orderFrontRegardless];
    }
}

- (id<CAMetalDrawable>)nextDrawable
{
    QMutexLocker locker(&_pendingLock);
    if (!_renderVisible || _layer.drawableSize.width < 1 || _layer.drawableSize.height < 1) {
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
    count++;
    if (count % 120 == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Seamless: main apply avg=%.1f us max=%.1f us over %d applies",
                    sum / count, max, count);
    }
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

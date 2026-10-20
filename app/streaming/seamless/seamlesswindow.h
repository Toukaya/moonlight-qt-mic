/**
 * @file app/streaming/seamless/seamlesswindow.h
 * @brief One local window that shows one remote window.
 * @details The page never draws into an existing stream view: a seamless session
 *          gets one ordinary local window per remote window, each with its own
 *          drawable, and the shared command buffer draws all of them in one
 *          commit. This class owns the window, its drawable and the rectangle of
 *          the canvas it samples; the renderer owns the command buffer.
 *
 *          Construction and every update must run on the main thread: the window
 *          server is a main-thread object, while the model is applied on the
 *          frame thread.
 */
#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <simd/simd.h>

#include <QtCore/QSize>

#include "seamlessmodel.h"

@interface SeamlessWindow : NSObject

/// Create the local window for one remote window id. No window is shown until
/// the first update gives it a size and a visible flag.
- (instancetype)initWithWindowId:(quint32)windowId device:(id<MTLDevice>)device;

/// Fit the window to the entry and remember where its content sits on the canvas.
- (void)updateWithEntry:(const SeamlessModel::WindowEntry &)entry
             canvasSize:(CGSize)canvasSize;

/// The drawable to render this frame into, or nil when the window is hidden.
- (id<CAMetalDrawable>)nextDrawable;

/// The canvas sub-rectangle this window samples, as x, y, w, h in 0..1.
- (void)copyCellUvTo:(simd_float4 *)out;

/// The remote window id this local window belongs to.
- (quint32)windowId;

/// Order the window out and drop its drawable. The window is not destroyed until
/// this is called, so a hidden (minimized) remote window keeps its place.
- (void)close;

@end

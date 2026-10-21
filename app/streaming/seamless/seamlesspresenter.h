/**
 * @file app/streaming/seamless/seamlesspresenter.h
 * @brief One local window per remote window, built ON TOP of unmodified renderer
 *        instances.
 * @details The renderer is never changed: each window gets its own instance of the
 *          same class the session uses, initialized with its own SDL window (the
 *          pristine contract VTMetalRenderer::initialize expects) and fed the same
 *          decoded frames. The cell crop is a layer translation with clipping, so
 *          the instance draws the full canvas and the host window shows the cell.
 *          The gate lives here, outside the renderer: nothing happens unless the
 *          session was started from the Seamless Windows page.
 */
#pragma once

#include <vector>

#include "seamlessmodel.h"

struct AVFrame;

namespace seamless_presenter {

/// The format the session decodes to; the presenter mirrors it rather than
/// choosing, so an instance converts exactly as the session's renderer does.
void setVideoFormat(int videoFormat, int frameRate);

/// Creates and destroys one local window (and its renderer instance) per table
/// entry. Main thread only. An empty table tears every window down.
void setTable(const std::vector<SeamlessModel::WindowEntry>& table);

/// Draws the frame into every local window. Main thread only. Does nothing when
/// the presentation mode is off or no window exists.
void present(AVFrame* frame);

/// Creates and destroys windows to match the applied table, only when it changed.
/// Same thread as present().
void syncFromModel();

/// Destroys every window and instance. Main thread only; also called at session
/// end by the mode switch.
void shutdown();

/// True when at least one local window exists.
bool active();

} // namespace seamless_presenter

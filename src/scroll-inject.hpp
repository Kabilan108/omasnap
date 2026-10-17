/** @fileoverview The auto-scroll injection worker: drives the application
 *  under the capture region with wheel input from a wlr virtual pointer bound
 *  to the target output, one tick per acknowledged capture cycle. */
#pragma once

#include "auto-capture.hpp"
#include "stitch.hpp"

#include <QString>

#include <atomic>
#include <memory>

/// Spawns the injection worker thread. `parkX/parkY` are physical pixels of
/// `outputName` where the pointer is parked so wheel events hit the page
/// inside the region. The worker publishes ready cycles on `handshake`
/// (cycle 1 = the unscrolled first frame), scrolls the acknowledged notch
/// count per cycle, and sets `stop` itself on any exit so its death is always
/// observable. Call off the GUI thread: connecting can block. Returns false
/// when cancelled, or with `error` set when the virtual pointer is unavailable.
[[nodiscard]] bool spawnScrollInjector(
    std::shared_ptr<std::atomic<bool>> stop,
    std::shared_ptr<stitch::CaptureHandshake> handshake, int parkX, int parkY,
    stitch::Axis axis, const QString &outputName, QString &error);

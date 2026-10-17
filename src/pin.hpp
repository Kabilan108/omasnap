#pragma once

#include <QString>

enum class PinLifetime { Timed, Persistent };

/** Runs a detached pinned-image layer on the requested Wayland output. */
[[nodiscard]] int runPinnedCapture(const QString &path, PinLifetime lifetime,
                                   const QString &outputName);

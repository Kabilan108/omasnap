#pragma once

#include <QString>

/** Runs a detached pinned-image layer on the requested Wayland output. */
[[nodiscard]] int runPinnedCapture(const QString &path,
                                   const QString &outputName);

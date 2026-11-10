#pragma once

#include <QString>

/** Saved-capture notifications: helper argv, actions, --notify, --recent. */
[[nodiscard]] bool runNotifySmoke(QString &error);
/** Editor output under --notify; runNotifySmoke prepares its fake commands. */
[[nodiscard]] bool runNotifyEditorSmoke(const QString &workPath, QString &error);

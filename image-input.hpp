#pragma once

#include <QString>

[[nodiscard]] bool resolveImagePath(const QString &input, QString &resolved,
                                    QString &error);

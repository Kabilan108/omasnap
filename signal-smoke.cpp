#include "signal-notifier.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>
#include <QTimer>

#include <csignal>
#include <unistd.h>

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  if (application.arguments().size() != 2)
    return 2;
  const QString requestedSignal = application.arguments().at(1);
  const int signal = requestedSignal == QStringLiteral("int")    ? SIGINT
                     : requestedSignal == QStringLiteral("term") ? SIGTERM
                                                                 : 0;
  if (signal == 0)
    return 2;

  QString markerPath;
  int eventLoopResult = 1;
  {
    QTemporaryFile marker;
    if (!marker.open())
      return 3;
    markerPath = marker.fileName();
    PosixSignalNotifier notifier(&application);
    QTimer::singleShot(0, &application,
                       [signal] { ::kill(::getpid(), signal); });
    QTimer::singleShot(2000, &application,
                       [&application] { application.exit(4); });
    eventLoopResult = application.exec();
  }
  if (eventLoopResult != 0 || QFile::exists(markerPath))
    return 5;
  return 0;
}

#include "signal-notifier.hpp"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

PosixSignalNotifier::PosixSignalNotifier(QObject *parent) : QObject(parent) {
  if (::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                   fds_) != 0)
    return;
  signalFd_ = fds_[0];

  struct sigaction action{};
  action.sa_handler = [](int) {
    const int savedErrno = errno;
    const char byte = 1;
    const int fd = signalFd_;
    if (fd >= 0)
      static_cast<void>(::write(fd, &byte, sizeof(byte)));
    errno = savedErrno;
  };
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigintInstalled_ = ::sigaction(SIGINT, &action, &previousSigint_) == 0;
  sigtermInstalled_ = ::sigaction(SIGTERM, &action, &previousSigterm_) == 0;
  if (!sigintInstalled_ && !sigtermInstalled_) {
    closeSockets();
    return;
  }

  notifier_ = new QSocketNotifier(fds_[1], QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this, [this] {
    notifier_->setEnabled(false);
    char bytes[32];
    while (::read(fds_[1], bytes, sizeof(bytes)) > 0) {
    }
    QCoreApplication::quit();
  });
}

PosixSignalNotifier::~PosixSignalNotifier() {
  if (sigintInstalled_)
    ::sigaction(SIGINT, &previousSigint_, nullptr);
  if (sigtermInstalled_)
    ::sigaction(SIGTERM, &previousSigterm_, nullptr);
  closeSockets();
}

void PosixSignalNotifier::closeSockets() {
  signalFd_ = -1;
  for (int &fd : fds_) {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }
}

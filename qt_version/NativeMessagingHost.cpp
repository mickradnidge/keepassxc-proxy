#include "NativeMessagingHost.h"
#include <QCoreApplication>

#ifndef Q_OS_LINUX
#if defined Q_OS_MAC || defined Q_OS_UNIX
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#endif
#ifdef Q_OS_LINUX
#include <sys/epoll.h>
#endif


NativeMessagingHost::NativeMessagingHost() :
    m_running(false)
{
#ifndef Q_OS_WIN
    m_notifier.reset(new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this));
    connect(m_notifier.data(), SIGNAL(activated(int)), this, SLOT(newMessage()));
    pid_t pid = getpid();
    QString client = "/tmp/kpxc_client." + QString::number(pid);
    QFile::remove(client);
#endif
    m_localSocket = new QLocalSocket();
#ifdef Q_OS_WIN
    m_localSocket->connectToServer("kpxc_server");
    m_running = true;
    m_future = QtConcurrent::run(this, &NativeMessagingHost::readNativeMessages);
#else
    m_localSocket->connectToServer("/tmp/kpxc_server");
#endif
    connect(m_localSocket, SIGNAL(readyRead()), this, SLOT(newLocalMessage()));
    connect(m_localSocket, SIGNAL(disconnected()), this, SLOT(deleteSocket()));
}

NativeMessagingHost::~NativeMessagingHost()
{
#ifdef Q_OS_WIN
    m_future.waitForFinished();
#endif
}

void NativeMessagingHost::newMessage()
{
#ifndef Q_OS_LINUX
#if defined Q_OS_MAC || defined Q_OS_UNIX
    struct kevent ev[1];
	struct timespec ts = { 5, 0 };

	int fd = kqueue();
	if (fd == -1) {
		m_notifier->setEnabled(false);
		return;
	}

	EV_SET(ev, fileno(stdin), EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(fd, ev, 1, NULL, 0, &ts) == -1) {
    	m_notifier->setEnabled(false);
    	return;
    }

    int ret = kevent(fd, NULL, 0, ev, 1, &ts);
    if (ret < 1) {
    	m_notifier->setEnabled(false);
        ::close(fd);
        return;
    }
#endif
#elif defined Q_OS_LINUX
    int fd = epoll_create(5);
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = 0;
    epoll_ctl(fd, EPOLL_CTL_ADD, 0, &event);

    epoll_wait(fd, &event, 1, -1);
#endif

    quint32 length = 0;
    std::cin.read(reinterpret_cast<char*>(&length), 4);

    if (!std::cin.eof() && length > 0)
    {
        QByteArray arr;
        for (quint32 i = 0; i < length; i++) {
            arr.append(getchar());
        }

        if (arr.length() > 0 && m_localSocket) {
            m_localSocket->write(arr.constData(), arr.length());
            m_localSocket->flush();
        }
    } else {
    	QCoreApplication::quit();
    }

#ifndef Q_OS_WIN
    ::close(fd);
#endif
}

#ifdef Q_OS_WIN
void NativeMessagingHost::readNativeMessages()
{
    quint32 length = 0;
    while (m_running && !std::cin.eof()) {
        length = 0;
        std::cin.read(reinterpret_cast<char*>(&length), 4);
        QByteArray arr;

        if (length > 0) {
            for (quint32 i = 0; i < length; i++) {
                arr.append(getchar());
            }

            if (arr.length() > 0 && m_localSocket) {
                m_localSocket->write(arr.constData(), arr.length());
                m_localSocket->flush();
            }
        } else {
            break;
        }

        QThread::msleep(1);
    }
}
#endif

void NativeMessagingHost::newLocalMessage()
{
    if (m_localSocket && m_localSocket->bytesAvailable() > 0) {
        QByteArray arr = m_localSocket->readAll();
        if (arr.length() > 0) {
           sendReply(arr);
        }
    }
}

void NativeMessagingHost::sendReply(const QString& reply)
{
    if (!reply.isEmpty()) {
        uint len = reply.length();
        std::cout << char(((len>>0) & 0xFF)) << char(((len>>8) & 0xFF)) << char(((len>>16) & 0xFF)) << char(((len>>24) & 0xFF));
        std::cout << reply.toStdString() << std::flush;
    }
}

void NativeMessagingHost::deleteSocket()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
    }
    m_localSocket->deleteLater();
    QCoreApplication::quit();
}

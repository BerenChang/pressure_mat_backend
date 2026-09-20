#include "PressureWebSocketServer.h"
#include "SerialReceiver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    SerialReceiver receiver;
    PressureWebSocketServer webSocketServer;

    quint64 framesThisSecond = 0;
    quint64 totalFrames = 0;

    QObject::connect(
        &receiver,
        &SerialReceiver::statusMessage,
        [](const QString &message) {
            qInfo().noquote() << "[SERIAL]" << message;
        });

    QObject::connect(
        &receiver,
        &SerialReceiver::serialError,
        [](const QString &message) {
            qCritical().noquote() << "[SERIAL ERROR]" << message;
        });

    QObject::connect(
        &webSocketServer,
        &PressureWebSocketServer::statusMessage,
        [](const QString &message) {
            qInfo().noquote() << "[WEBSOCKET]" << message;
        });

    QObject::connect(
        &webSocketServer,
        &PressureWebSocketServer::serverError,
        [](const QString &message) {
            qCritical().noquote() << "[WEBSOCKET ERROR]" << message;
        });

    QObject::connect(
        &receiver,
        &SerialReceiver::frameReceived,
        &webSocketServer,
        &PressureWebSocketServer::broadcastFrame);

    QObject::connect(
        &receiver,
        &SerialReceiver::frameReceived,
        [&](const QByteArray &) {
            ++framesThisSecond;
            ++totalFrames;
        });

    QObject::connect(
        &webSocketServer,
        &PressureWebSocketServer::clientCommandReceived,
        [&](const QString &command) {
            if (command == QStringLiteral("start_reading")) {
                receiver.startReading();
            } else if (
                command == QStringLiteral("stop_reading")
                ) {
                receiver.stopReading();
            } else {
                qWarning().noquote()
                << "[WEBSOCKET] Unknown command:"
                << command;
            }
        });

    QTimer reportTimer;

    QObject::connect(
        &reportTimer,
        &QTimer::timeout,
        [&]() {
            qInfo() << "Frames/s:" << framesThisSecond
                    << "| Total:" << totalFrames
                    << "| Browser clients:"
                    << webSocketServer.clientCount();

            framesThisSecond = 0;
        });

    if (!webSocketServer.listen(9002)) {
        return 1;
    }

    if (!receiver.openPort("COM3", 115200)) {
        webSocketServer.close();
        return 1;
    }

    receiver.startReading();
    reportTimer.start(1000);

    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        [&]() {
            reportTimer.stop();
            receiver.closePort();
            webSocketServer.close();
        });

    return app.exec();
}
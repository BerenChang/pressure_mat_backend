#include "PressureWebSocketServer.h"
#include "SerialReceiver.h"
#include "StaticHttpServer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QProcess>

namespace
{
QString findFrontendDirectory()
{
    const QDir applicationDirectory(
        QCoreApplication::applicationDirPath());

    const QStringList candidates{
        // Final packaged location.
        applicationDirectory.filePath(
            QStringLiteral("frontend")),

        // Qt Creator development-build location.
        applicationDirectory.filePath(
            QStringLiteral(
                "../../frontend/dist")),

        // Additional development fallback.
        QDir::current().filePath(
            QStringLiteral("frontend/dist"))
    };

    for (const QString &candidate : candidates) {
        const QFileInfo indexFile(
            QDir(candidate).filePath(
                QStringLiteral("index.html")));

        if (indexFile.isFile()) {
            return QDir(candidate).canonicalPath();
        }
    }

    return {};
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    SerialReceiver receiver;
    PressureWebSocketServer webSocketServer;
    StaticHttpServer httpServer;

    quint64 framesThisSecond = 0;
    quint64 totalFrames = 0;

    QObject::connect(
        &httpServer,
        &StaticHttpServer::statusMessage,
        [](const QString &message) {
            qInfo().noquote()
            << "[HTTP]" << message;
        });

    QObject::connect(
        &httpServer,
        &StaticHttpServer::serverError,
        [](const QString &message) {
            qCritical().noquote()
            << "[HTTP ERROR]" << message;
        });

    QObject::connect(
        &receiver,
        &SerialReceiver::statusMessage,
        [](const QString &message) {
            qInfo().noquote() << "[SERIAL]" << message;
        });

        QObject::connect(
            &receiver,
            &SerialReceiver::serialError,
            [&](const QString &message) {
                qCritical().noquote()
                    << "[SERIAL ERROR]" << message;
        
                QJsonObject errorMessage;
                errorMessage.insert(
                    QStringLiteral("type"),
                    QStringLiteral("serial_error"));
                errorMessage.insert(
                    QStringLiteral("message"),
                    message);
        
                webSocketServer.broadcastTextMessage(
                    QString::fromUtf8(
                        QJsonDocument(errorMessage).toJson(
                            QJsonDocument::Compact)));
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

    QString activePort;
    bool readingActive = false;

    auto sendSerialStatus = [&]() {
        QJsonObject status;
        status.insert(
            QStringLiteral("type"),
            QStringLiteral("serial_status"));
        status.insert(
            QStringLiteral("connected"),
            receiver.isOpen());
        status.insert(
            QStringLiteral("reading"),
            readingActive);
        status.insert(
            QStringLiteral("port"),
            activePort);

        webSocketServer.broadcastTextMessage(
            QString::fromUtf8(
                QJsonDocument(status).toJson(
                    QJsonDocument::Compact)));
    };

    auto sendPortList = [&]() {
        QJsonArray ports;

        for (
            const QString &portName :
            SerialReceiver::availablePortNames()
            ) {
            ports.append(portName);
        }

        QJsonArray baudRates;
        baudRates.append(115200);
        baudRates.append(961200);

        QJsonObject message;
        message.insert(
            QStringLiteral("type"),
            QStringLiteral("serial_ports"));
        message.insert(
            QStringLiteral("ports"),
            ports);
        message.insert(
            QStringLiteral("baudRates"),
            baudRates);

        webSocketServer.broadcastTextMessage(
            QString::fromUtf8(
                QJsonDocument(message).toJson(
                    QJsonDocument::Compact)));
    };

    QObject::connect(
        &receiver,
        &SerialReceiver::connectionChanged,
        [&](bool connected) {
            if (connected) {
                return;
            }

            readingActive = false;
            activePort.clear();
            sendSerialStatus();
        });

    QObject::connect(
        &webSocketServer,
        &PressureWebSocketServer::clientCommandReceived,
        [&](const QString &message) {
            if (message == QStringLiteral("start_reading")) {
                if (receiver.isOpen()) {
                    receiver.startReading();
                    readingActive = true;
                }

                sendSerialStatus();
                return;
            }

            if (message == QStringLiteral("stop_reading")) {
                receiver.stopReading();
                readingActive = false;
                sendSerialStatus();
                return;
            }

            const QJsonDocument document =
                QJsonDocument::fromJson(message.toUtf8());

            if (!document.isObject()) {
                qWarning().noquote()
                << "[WEBSOCKET] Invalid command:"
                << message;
                return;
            }

            const QJsonObject command = document.object();
            const QString action =
                command.value(
                           QStringLiteral("command")).toString();

            if (action == QStringLiteral("connect")) {
                const QString portName =
                    command.value(
                               QStringLiteral("port")).toString();

                const qint32 baudRate =
                    static_cast<qint32>(
                        command.value(
                                   QStringLiteral("baudRate"))
                            .toInt(115200));

                readingActive = false;

                if (receiver.openPort(portName, baudRate)) {
                    activePort = portName;
                } else {
                    activePort.clear();
                }

                sendSerialStatus();
                return;
            }

            if (action == QStringLiteral("disconnect")) {
                receiver.closePort();
                activePort.clear();
                readingActive = false;
                sendSerialStatus();
                return;
            }

            if (action == QStringLiteral("refresh_ports")) {
                sendPortList();
                return;
            }

            qWarning().noquote()
                << "[WEBSOCKET] Unknown command:"
                << message;
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

    QObject::connect(
        &webSocketServer,
        &PressureWebSocketServer::clientCountChanged,
        [&](int clientCount) {
            if (clientCount <= 0) {
                return;
            }

            sendPortList();
            sendSerialStatus();
        });

    if (!webSocketServer.listen(9002)) {
        return 1;
    }

    const QString frontendDirectory =
        findFrontendDirectory();

    if (frontendDirectory.isEmpty()) {
        qCritical()
        << "Could not find frontend/dist/index.html";

        webSocketServer.close();
        return 1;
    }

    if (!httpServer.listen(frontendDirectory, 8080)) {
        webSocketServer.close();
        return 1;
    }

    QTimer::singleShot(
        250,
        []() {
            const QString dashboardUrl =
                QStringLiteral(
                    "http://127.0.0.1:8080");

            if (
                !QProcess::startDetached(
                    QStringLiteral("explorer.exe"),
                    QStringList{dashboardUrl})
                ) {
                qWarning()
                << "Could not open dashboard:"
                << dashboardUrl;
            }
        });

    reportTimer.start(1000);

    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        [&]() {
            reportTimer.stop();
            receiver.closePort();
            webSocketServer.close();
            httpServer.close();
        });

    return app.exec();
}
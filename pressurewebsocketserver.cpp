#include "PressureWebSocketServer.h"

#include "PressureFrameParser.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QWebSocket>

PressureWebSocketServer::PressureWebSocketServer(QObject *parent)
    : QObject(parent),
    server_(
        QStringLiteral("Pressure Mat WebSocket Server"),
        QWebSocketServer::NonSecureMode,
        this)
{
    connect(
        &server_,
        &QWebSocketServer::newConnection,
        this,
        &PressureWebSocketServer::handleNewConnection);

    connect(
        &server_,
        &QWebSocketServer::acceptError,
        this,
        [this](QAbstractSocket::SocketError) {
            emit serverError(server_.errorString());
        });
}

bool PressureWebSocketServer::listen(quint16 port)
{
    if (server_.isListening()) {
        return true;
    }

    if (!server_.listen(QHostAddress::LocalHost, port)) {
        emit serverError(
            QStringLiteral("WebSocket server failed: %1")
                .arg(server_.errorString()));

        return false;
    }

    emit statusMessage(
        QStringLiteral("WebSocket server listening at ws://127.0.0.1:%1")
            .arg(server_.serverPort()));

    return true;
}

void PressureWebSocketServer::close()
{
    if (!server_.isListening()) {
        return;
    }

    const QList<QWebSocket *> clients = clients_;

    for (QWebSocket *client : clients) {
        client->close(QWebSocketProtocol::CloseCodeGoingAway);
        client->deleteLater();
    }

    clients_.clear();
    server_.close();

    emit clientCountChanged(0);
    emit statusMessage(QStringLiteral("WebSocket server closed"));
}

bool PressureWebSocketServer::isListening() const
{
    return server_.isListening();
}

int PressureWebSocketServer::clientCount() const
{
    return clients_.size();
}

void PressureWebSocketServer::broadcastFrame(
    const QByteArray &payload)
{
    for (QWebSocket *client : clients_) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->sendBinaryMessage(payload);
        }
    }
}

void PressureWebSocketServer::handleNewConnection()
{
    while (server_.hasPendingConnections()) {
        QWebSocket *client = server_.nextPendingConnection();

        if (client == nullptr) {
            continue;
        }

        clients_.append(client);

        connect(
            client,
            &QWebSocket::disconnected,
            this,
            &PressureWebSocketServer::handleClientDisconnected);

        connect(
            client,
            &QWebSocket::textMessageReceived,
            this,
            &PressureWebSocketServer::handleTextMessage);

        client->sendTextMessage(
            QStringLiteral(
                R"({"type":"configuration","rows":%1,"columns":%2,"format":"uint8"})")
                .arg(PressureFrameParser::Rows)
                .arg(PressureFrameParser::Columns));

        emit clientCountChanged(clients_.size());
        emit statusMessage(
            QStringLiteral("Browser connected; clients: %1")
                .arg(clients_.size()));
    }
}

void PressureWebSocketServer::handleClientDisconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());

    if (client == nullptr) {
        return;
    }

    clients_.removeAll(client);
    client->deleteLater();

    emit clientCountChanged(clients_.size());
    emit statusMessage(
        QStringLiteral("Browser disconnected; clients: %1")
            .arg(clients_.size()));
}

void PressureWebSocketServer::handleTextMessage(
    const QString &message)
{
    emit clientCommandReceived(message);
}
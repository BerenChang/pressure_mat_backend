#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QWebSocketServer>

class QWebSocket;

class PressureWebSocketServer : public QObject
{
    Q_OBJECT

public:
    explicit PressureWebSocketServer(QObject *parent = nullptr);

    bool listen(quint16 port);
    void close();

    [[nodiscard]] bool isListening() const;
    [[nodiscard]] int clientCount() const;

public slots:
    void broadcastFrame(const QByteArray &payload);
    void broadcastTextMessage(const QString &message);

signals:
    void clientCountChanged(int count);
    void clientCommandReceived(const QString &message);
    void statusMessage(const QString &message);
    void serverError(const QString &message);

private slots:
    void handleNewConnection();
    void handleClientDisconnected();
    void handleTextMessage(const QString &message);

private:
    QWebSocketServer server_;
    QList<QWebSocket *> clients_;
};
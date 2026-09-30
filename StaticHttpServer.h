#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

class StaticHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit StaticHttpServer(QObject *parent = nullptr);

    bool listen(
        const QString &documentRoot,
        quint16 port);

    void close();

    [[nodiscard]] bool isListening() const;
    [[nodiscard]] quint16 serverPort() const;

signals:
    void statusMessage(const QString &message);
    void serverError(const QString &message);

private slots:
    void handleNewConnection();
    void handleReadyRead();
    void handleDisconnected();

private:
    void processRequest(
        QTcpSocket *socket,
        const QByteArray &request);

    void sendResponse(
        QTcpSocket *socket,
        int statusCode,
        const QByteArray &statusText,
        const QByteArray &contentType,
        const QByteArray &body);

    [[nodiscard]] static QByteArray contentTypeForFile(
        const QString &filePath);

    QTcpServer server_;
    QString documentRoot_;
    QHash<QTcpSocket *, QByteArray> requestBuffers_;
};
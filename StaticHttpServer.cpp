#include "StaticHttpServer.h"

#include <QAbstractSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUrl>

StaticHttpServer::StaticHttpServer(QObject *parent)
    : QObject(parent)
{
    connect(
        &server_,
        &QTcpServer::newConnection,
        this,
        &StaticHttpServer::handleNewConnection);

    connect(
        &server_,
        &QTcpServer::acceptError,
        this,
        [this](QAbstractSocket::SocketError) {
            emit serverError(server_.errorString());
        });
}

bool StaticHttpServer::listen(
    const QString &documentRoot,
    quint16 port)
{
    close();

    const QDir rootDirectory(documentRoot);
    const QFileInfo indexFile(
        rootDirectory.filePath(
            QStringLiteral("index.html")));

    if (
        !rootDirectory.exists() ||
        !indexFile.isFile()
        ) {
        emit serverError(
            QStringLiteral(
                "Frontend index.html not found in: %1")
                .arg(documentRoot));

        return false;
    }

    documentRoot_ =
        QFileInfo(rootDirectory.absolutePath())
            .canonicalFilePath();

    if (documentRoot_.isEmpty()) {
        emit serverError(
            QStringLiteral(
                "Invalid frontend directory: %1")
                .arg(documentRoot));

        return false;
    }

    if (
        !server_.listen(
            QHostAddress::LocalHost,
            port)
        ) {
        emit serverError(
            QStringLiteral(
                "HTTP server failed: %1")
                .arg(server_.errorString()));

        return false;
    }

    emit statusMessage(
        QStringLiteral(
            "HTTP server listening at "
            "http://127.0.0.1:%1")
            .arg(server_.serverPort()));

    return true;
}

void StaticHttpServer::close()
{
    const bool wasListening =
        server_.isListening();

    const QList<QTcpSocket *> sockets =
        requestBuffers_.keys();

    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
    }

    requestBuffers_.clear();
    server_.close();
    documentRoot_.clear();

    if (wasListening) {
        emit statusMessage(
            QStringLiteral("HTTP server closed"));
    }
}

bool StaticHttpServer::isListening() const
{
    return server_.isListening();
}

quint16 StaticHttpServer::serverPort() const
{
    return server_.serverPort();
}

void StaticHttpServer::handleNewConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket =
            server_.nextPendingConnection();

        if (socket == nullptr) {
            continue;
        }

        requestBuffers_.insert(
            socket,
            QByteArray{});

        connect(
            socket,
            &QTcpSocket::readyRead,
            this,
            &StaticHttpServer::handleReadyRead);

        connect(
            socket,
            &QTcpSocket::disconnected,
            this,
            &StaticHttpServer::handleDisconnected);
    }
}

void StaticHttpServer::handleReadyRead()
{
    QTcpSocket *socket =
        qobject_cast<QTcpSocket *>(sender());

    if (
        socket == nullptr ||
        !requestBuffers_.contains(socket)
        ) {
        return;
    }

    QByteArray &buffer =
        requestBuffers_[socket];

    buffer.append(socket->readAll());

    constexpr qsizetype MaximumRequestSize =
        16 * 1024;

    if (buffer.size() > MaximumRequestSize) {
        sendResponse(
            socket,
            413,
            "Payload Too Large",
            "text/plain; charset=utf-8",
            "Request is too large.");

        return;
    }

    const qsizetype headerEnd =
        buffer.indexOf("\r\n\r\n");

    if (headerEnd < 0) {
        return;
    }

    const QByteArray request =
        buffer.left(headerEnd + 4);

    processRequest(socket, request);
}

void StaticHttpServer::handleDisconnected()
{
    QTcpSocket *socket =
        qobject_cast<QTcpSocket *>(sender());

    if (socket == nullptr) {
        return;
    }

    requestBuffers_.remove(socket);
    socket->deleteLater();
}

void StaticHttpServer::processRequest(
    QTcpSocket *socket,
    const QByteArray &request)
{
    const qsizetype firstLineEnd =
        request.indexOf("\r\n");

    if (firstLineEnd < 0) {
        sendResponse(
            socket,
            400,
            "Bad Request",
            "text/plain; charset=utf-8",
            "Malformed HTTP request.");

        return;
    }

    const QList<QByteArray> requestParts =
        request.left(firstLineEnd).split(' ');

    if (
        requestParts.size() < 3 ||
        requestParts.at(0) != "GET"
        ) {
        sendResponse(
            socket,
            405,
            "Method Not Allowed",
            "text/plain; charset=utf-8",
            "Only GET requests are supported.");

        return;
    }

    const QUrl requestUrl =
        QUrl::fromEncoded(requestParts.at(1));

    QString relativePath =
        QDir::cleanPath(requestUrl.path());

    while (relativePath.startsWith('/')) {
        relativePath.remove(0, 1);
    }

    if (
        relativePath.isEmpty() ||
        relativePath == QStringLiteral(".")
        ) {
        relativePath =
            QStringLiteral("index.html");
    }

    const QString candidatePath =
        QDir(documentRoot_)
            .absoluteFilePath(relativePath);

    const QFileInfo requestedFile(candidatePath);
    const QString canonicalPath =
        requestedFile.canonicalFilePath();

    const QString normalizedRoot =
        QDir::fromNativeSeparators(documentRoot_);

    const QString normalizedFile =
        QDir::fromNativeSeparators(canonicalPath);

    const QString allowedPrefix =
        normalizedRoot + '/';

    if (
        normalizedFile.isEmpty() ||
        !normalizedFile.startsWith(
            allowedPrefix,
            Qt::CaseInsensitive) ||
        !requestedFile.isFile()
        ) {
        sendResponse(
            socket,
            404,
            "Not Found",
            "text/plain; charset=utf-8",
            "File not found.");

        return;
    }

    QFile file(canonicalPath);

    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(
            socket,
            500,
            "Internal Server Error",
            "text/plain; charset=utf-8",
            "Could not read the requested file.");

        return;
    }

    sendResponse(
        socket,
        200,
        "OK",
        contentTypeForFile(canonicalPath),
        file.readAll());
}

void StaticHttpServer::sendResponse(
    QTcpSocket *socket,
    int statusCode,
    const QByteArray &statusText,
    const QByteArray &contentType,
    const QByteArray &body)
{
    QByteArray response;

    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(statusText);
    response.append("\r\n");

    response.append("Content-Type: ");
    response.append(contentType);
    response.append("\r\n");

    response.append("Content-Length: ");
    response.append(
        QByteArray::number(body.size()));
    response.append("\r\n");

    response.append(
        "Cache-Control: no-cache\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n"
        "\r\n");

    response.append(body);

    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray StaticHttpServer::contentTypeForFile(
    const QString &filePath)
{
    const QString suffix =
        QFileInfo(filePath).suffix().toLower();

    if (suffix == QStringLiteral("html")) {
        return "text/html; charset=utf-8";
    }

    if (
        suffix == QStringLiteral("js") ||
        suffix == QStringLiteral("mjs")
        ) {
        return "text/javascript; charset=utf-8";
    }

    if (suffix == QStringLiteral("css")) {
        return "text/css; charset=utf-8";
    }

    if (suffix == QStringLiteral("json")) {
        return "application/json; charset=utf-8";
    }

    if (suffix == QStringLiteral("svg")) {
        return "image/svg+xml";
    }

    if (suffix == QStringLiteral("png")) {
        return "image/png";
    }

    if (
        suffix == QStringLiteral("jpg") ||
        suffix == QStringLiteral("jpeg")
        ) {
        return "image/jpeg";
    }

    if (suffix == QStringLiteral("ico")) {
        return "image/x-icon";
    }

    if (suffix == QStringLiteral("wasm")) {
        return "application/wasm";
    }

    return "application/octet-stream";
}
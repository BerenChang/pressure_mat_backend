#pragma once

#include "snapshotbuffer.h"
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QList>
#include <QJsonObject>
#include <memory>

class SnapshotService : public QObject {
    Q_OBJECT
public:
    explicit SnapshotService(QObject *parent = nullptr);
    bool listen(quint16 port = 9003);
    quint16 port() const { return socket_.localPort(); }
    QString lastSnapshot() const { return lastSnapshot_; }
    void setActive(bool active);
    void addFrame(const QByteArray &payload, qint64 time, bool batched);
signals:
    void messageReady(const QString &json);
private:
    struct Request { quint64 id; qint64 target; QHostAddress peer; quint16 port; };
    void readTriggers();
    void resolve();
    void fail(const Request &request, const QString &reason);
    void publish(const Request &request, const QJsonObject &message);
    QUdpSocket socket_;
    QTimer expiry_;
    std::unique_ptr<SnapshotBuffer> buffer_ = std::make_unique<SnapshotBuffer>();
    QList<Request> pending_;
    quint64 nextId_ = 0;
    bool active_ = false;
    QString lastSnapshot_;
};

#include "snapshotservice.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>

namespace {
constexpr qint64 TimeoutNs = 250'000'000;
constexpr int MaxPending = 32;
}

SnapshotService::SnapshotService(QObject *parent) : QObject(parent) {
    connect(&socket_, &QUdpSocket::readyRead, this, &SnapshotService::readTriggers);
    connect(&expiry_, &QTimer::timeout, this, &SnapshotService::resolve);
    expiry_.setInterval(10);
}

bool SnapshotService::listen(quint16 port) {
    return socket_.bind(QHostAddress::LocalHost, port, QAbstractSocket::DontShareAddress);
}

void SnapshotService::setActive(bool active) {
    active_ = active;
    buffer_->clear();
    const auto requests = pending_;
    pending_.clear();
    expiry_.stop();
    for (const auto &request : requests) fail(request, "Acquisition changed");
}

void SnapshotService::addFrame(const QByteArray &payload, qint64 time, bool batched) {
    if (!active_ || payload.size() != SnapshotBuffer::Cells) return;
    if (batched) {
        buffer_->clear();
        const auto requests = pending_;
        pending_.clear();
        expiry_.stop();
        for (const auto &request : requests) fail(request, "Batched frames: timing uncertain");
        return;
    }
    SnapshotBuffer::Values values{};
    for (int i = 0; i < SnapshotBuffer::Cells; ++i)
        values[i] = static_cast<unsigned char>(payload[i]);
    if (buffer_->append(time, values)) resolve();
}

void SnapshotService::publish(const Request &request, const QJsonObject &message) {
    QJsonObject envelope = message;
    envelope.insert("id", QString::number(request.id));
    const QByteArray json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    socket_.writeDatagram(json, request.peer, request.port);
    emit messageReady(QString::fromUtf8(json));
}

void SnapshotService::fail(const Request &request, const QString &reason) {
    publish(request, {{"type", "snapshot_error"}, {"message", reason}});
}

void SnapshotService::readTriggers() {
    // Bound each turn so a burst of datagrams cannot starve serial acquisition.
    for (int i = 0; i < 64 && socket_.hasPendingDatagrams(); ++i) {
        const auto time = SnapshotBuffer::nowNs();
        const auto size = socket_.pendingDatagramSize();
        const auto datagram = socket_.receiveDatagram(64);
        if (!datagram.isValid() || !datagram.senderAddress().isLoopback()) continue;
        Request request{++nextId_, time, datagram.senderAddress(), static_cast<quint16>(datagram.senderPort())};
        if (size > 64 || datagram.data().trimmed() != "SNAPSHOT") {
            fail(request, "Expected SNAPSHOT");
        } else if (!active_ || !buffer_->size()) {
            fail(request, "Start acquisition and wait for a frame");
        } else if (time - buffer_->latestTime() > SnapshotBuffer::MaxGapNs) {
            fail(request, "Latest frame is stale");
        } else if (pending_.size() >= MaxPending) {
            fail(request, "Snapshot queue is full");
        } else {
            pending_.append(request);
            publish(request, {{"type", "snapshot_pending"}});
            expiry_.start();
        }
    }
    resolve();
    if (socket_.hasPendingDatagrams())
        QTimer::singleShot(0, this, &SnapshotService::readTriggers);
}

void SnapshotService::resolve() {
    const auto now = SnapshotBuffer::nowNs();
    for (qsizetype i = 0; i < pending_.size();) {
        const auto request = pending_[i];
        if (now - request.target > TimeoutNs) {
            pending_.removeAt(i);
            fail(request, "Timed out waiting for a later frame");
        } else if (buffer_->size() && buffer_->latestTime() >= request.target) {
            pending_.removeAt(i);
            const auto result = buffer_->interpolate(request.target);
            if (!result) {
                fail(request, "No valid bracketing frames (gap exceeds 100 ms)");
                continue;
            }
            QJsonArray values;
            for (const double value : result->values) values.append(value);
            QJsonObject snapshot{
                {"type", "snapshot"}, {"id", QString::number(request.id)},
                {"rows", 28}, {"columns", 56}, {"layout", "column-major"},
                {"timing", "host-receipt-estimate"},
                {"triggerNs", QString::number(request.target)},
                {"beforeNs", QString::number(result->before)},
                {"afterNs", QString::number(result->after)},
                {"gapMs", double(result->after - result->before) / 1e6},
                {"alpha", result->alpha}, {"values", values}
            };
            lastSnapshot_ = QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
            publish(request, snapshot);
        } else {
            ++i;
        }
    }
    if (pending_.isEmpty()) expiry_.stop();
}

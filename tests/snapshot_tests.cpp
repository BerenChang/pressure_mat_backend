#include "snapshotbuffer.h"
#include "snapshotservice.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <memory>

class SnapshotTests : public QObject {
    Q_OBJECT
private:
    static QJsonObject message(const QSignalSpy &spy, int index) {
        return QJsonDocument::fromJson(spy[index][0].toString().toUtf8()).object();
    }
    static void frame(SnapshotService &service, int value, qint64 offset = 0, bool batched = false) {
        service.addFrame(QByteArray(1568, char(value)), SnapshotBuffer::nowNs() + offset, batched);
    }
    static void trigger(QUdpSocket &client, const SnapshotService &service, QByteArray text = "SNAPSHOT") {
        QCOMPARE(client.writeDatagram(text, QHostAddress::LocalHost, service.port()), qint64(text.size()));
    }
private slots:
    void interpolationAndBoundaries() {
        auto buffer = std::make_unique<SnapshotBuffer>();
        SnapshotBuffer::Values a{}, b{};
        a.fill(10); b.fill(210); b[1567] = 110;
        QVERIFY(!buffer->interpolate(100));
        QVERIFY(buffer->append(100, a));
        QVERIFY(buffer->append(20'000'100, b));
        QVERIFY(!buffer->append(20'000'100, b));
        QVERIFY(!buffer->append(1, b));
        auto result = buffer->interpolate(5'000'100);
        QVERIFY(result);
        QCOMPARE(result->alpha, .25);
        QCOMPARE(result->values[0], 60.0);
        QCOMPARE(result->values[1567], 35.0);
        QCOMPARE(buffer->interpolate(100)->values[0], 10.0);
        QCOMPARE(buffer->interpolate(20'000'100)->values[0], 210.0);
        QVERIFY(!buffer->interpolate(99));
        QVERIFY(!buffer->interpolate(20'000'101));
        buffer->append(200'000'100, a);
        QVERIFY(!buffer->interpolate(50'000'100));
        buffer->clear();
        QCOMPARE(buffer->size(), 0);
    }
    void circularWrap() {
        auto buffer = std::make_unique<SnapshotBuffer>();
        SnapshotBuffer::Values values{};
        for (int i = 0; i < 600; ++i) {
            values.fill(i);
            QVERIFY(buffer->append(i * 20'000'000LL, values));
        }
        QCOMPARE(buffer->size(), SnapshotBuffer::Capacity);
        QVERIFY(!buffer->interpolate(343 * 20'000'000LL));
        QCOMPARE(buffer->interpolate(344 * 20'000'000LL)->values[0], 344.0);
        QCOMPARE(buffer->interpolate(598 * 20'000'000LL + 10'000'000)->values[0], 598.5);
    }
    void udpInterpolatesAndRetains() {
        SnapshotService service;
        QVERIFY(service.listen(0));
        service.setActive(true);
        QSignalSpy spy(&service, &SnapshotService::messageReady);
        QUdpSocket client;
        frame(service, 10, -20'000'000);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(message(spy, 0)["type"].toString(), "snapshot_pending");
        // A trigger alone never freezes the most recent frame.
        QVERIFY(service.lastSnapshot().isEmpty());
        frame(service, 110, 1'000'000);
        QTRY_COMPARE(spy.size(), 2);
        const auto result = message(spy, 1);
        QCOMPARE(result["type"].toString(), "snapshot");
        const auto target = result["triggerNs"].toString().toLongLong();
        const auto before = result["beforeNs"].toString().toLongLong();
        const auto after = result["afterNs"].toString().toLongLong();
        QVERIFY(before < target && target < after);
        const double expected = 10.0 + 100.0 * double(target - before) / double(after - before);
        QCOMPARE(result["values"].toArray().size(), 1568);
        QCOMPARE(result["values"].toArray()[0].toDouble(), expected);
        QTRY_VERIFY(client.hasPendingDatagrams());
        QCOMPARE(QJsonDocument::fromJson(client.receiveDatagram().data()).object()["type"].toString(), "snapshot_pending");
        QTRY_VERIFY(client.hasPendingDatagrams());
        QCOMPARE(QJsonDocument::fromJson(client.receiveDatagram().data()).object()["type"].toString(), "snapshot");
        const auto frozen = service.lastSnapshot();
        frame(service, 220, 2'000'000);
        QCOMPARE(service.lastSnapshot(), frozen);
        service.setActive(false);
        QCOMPARE(service.lastSnapshot(), frozen);
    }
    void invalidAndInactive() {
        SnapshotService service;
        QVERIFY(service.listen(0));
        QSignalSpy spy(&service, &SnapshotService::messageReady);
        QUdpSocket client;
        trigger(client, service, "WRONG");
        trigger(client, service, QByteArray(100, 'X'));
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(message(spy, i)["type"].toString(), "snapshot_error");
    }
    void staleTimeoutGapAndReset() {
        SnapshotService service;
        QVERIFY(service.listen(0));
        QSignalSpy spy(&service, &SnapshotService::messageReady);
        QUdpSocket client;
        service.setActive(true);
        frame(service, 1, -200'000'000);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(message(spy, 0)["message"].toString().contains("stale"));
        frame(service, 1);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 3, 1000);
        QVERIFY(message(spy, 2)["message"].toString().contains("Timed out"));
        frame(service, 2);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 4);
        frame(service, 3, 200'000'000);
        QTRY_COMPARE(spy.size(), 5);
        QVERIFY(message(spy, 4)["message"].toString().contains("bracketing"));
        service.setActive(true);
        frame(service, 2);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 6);
        service.setActive(false);
        QCOMPARE(spy.size(), 7);
        QVERIFY(message(spy, 6)["message"].toString().contains("changed"));
        service.setActive(true);
        frame(service, 2);
        trigger(client, service);
        QTRY_COMPARE(spy.size(), 8);
        frame(service, 3, 0, true);
        QCOMPARE(spy.size(), 9);
        QVERIFY(message(spy, 8)["message"].toString().contains("Batched"));
    }
    void burstsAndExclusiveBind() {
        SnapshotService service, other;
        QVERIFY(service.listen(0));
        QVERIFY(!other.listen(service.port()));
        service.setActive(true);
        frame(service, 2);
        QSignalSpy spy(&service, &SnapshotService::messageReady);
        QUdpSocket client;
        for (int i = 0; i < 40; ++i) trigger(client, service);
        QTRY_COMPARE(spy.size(), 40);
        int queued = 0, rejected = 0;
        for (int i = 0; i < 40; ++i) {
            if (message(spy, i)["type"] == "snapshot_pending") ++queued;
            else ++rejected;
        }
        QCOMPARE(queued, 32);
        QCOMPARE(rejected, 8);
        frame(service, 3, 1'000'000);
        QTRY_COMPARE(spy.size(), 72);
    }
    void liveFramesContinueDuringCaptures() {
        SnapshotService service;
        QVERIFY(service.listen(0));
        service.setActive(true);
        QUdpSocket client;
        QSignalSpy spy(&service, &SnapshotService::messageReady);
        QTimer acquisition;
        acquisition.setTimerType(Qt::PreciseTimer);
        acquisition.setInterval(20); // Simulated 50 Hz acquisition.
        int frames = 0;
        connect(&acquisition, &QTimer::timeout, [&] {
            frame(service, ++frames % 256);
            if (frames % 3 == 0 && frames <= 18) trigger(client, service);
        });
        acquisition.start();
        QTRY_VERIFY_WITH_TIMEOUT(frames >= 21, 2000);
        acquisition.stop();
        int completed = 0;
        for (int i = 0; i < spy.size(); ++i) {
            QVERIFY(message(spy, i)["type"].toString() != "snapshot_error");
            if (message(spy, i)["type"].toString() == "snapshot") ++completed;
        }
        QCOMPARE(completed, 6);
    }
};
QTEST_GUILESS_MAIN(SnapshotTests)
#include "snapshot_tests.moc"

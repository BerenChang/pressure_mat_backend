#pragma once

#include "PressureFrameParser.h"

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>

class SerialReceiver : public QObject
{
    Q_OBJECT

public:
    explicit SerialReceiver(QObject *parent = nullptr);

    static QStringList availablePortNames();

    bool openPort(const QString &portName, qint32 baudRate);
    void closePort();

    [[nodiscard]] bool isOpen() const;

    void startReading();
    void stopReading();

signals:
    void frameReceived(const QByteArray &payload);
    void connectionChanged(bool connected);
    void statusMessage(const QString &message);
    void serialError(const QString &message);

private slots:
    void handleReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    void sendCommand(const QByteArray &command);

    QSerialPort serialPort_;
    PressureFrameParser parser_;
};
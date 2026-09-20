#include "SerialReceiver.h"

#include <QIODevice>
#include <QSerialPortInfo>
// #include <QDebug>

SerialReceiver::SerialReceiver(QObject *parent)
    : QObject(parent)
{
    connect(
        &serialPort_,
        &QSerialPort::readyRead,
        this,
        &SerialReceiver::handleReadyRead);

    connect(
        &serialPort_,
        &QSerialPort::errorOccurred,
        this,
        &SerialReceiver::handleSerialError);
}

QStringList SerialReceiver::availablePortNames()
{
    QStringList names;

    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        names.append(port.portName());
    }

    names.sort();
    return names;
}

bool SerialReceiver::openPort(
    const QString &portName,
    qint32 baudRate)
{
    if (serialPort_.isOpen()) {
        closePort();
    }

    parser_.reset();

    serialPort_.setPortName(portName);
    serialPort_.setBaudRate(baudRate);
    serialPort_.setDataBits(QSerialPort::Data8);
    serialPort_.setParity(QSerialPort::NoParity);
    serialPort_.setStopBits(QSerialPort::OneStop);
    serialPort_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serialPort_.open(QIODevice::ReadWrite)) {
        emit serialError(
            QStringLiteral("Failed to open %1: %2")
                .arg(portName, serialPort_.errorString()));

        return false;
    }

    if (!serialPort_.setDataTerminalReady(true)) {
        emit statusMessage(
            QStringLiteral("Warning: could not enable DTR: %1")
                .arg(serialPort_.errorString()));
    }

    emit connectionChanged(true);
    emit statusMessage(
        QStringLiteral("Opened %1 at %2 baud")
            .arg(portName)
            .arg(baudRate));

    return true;
}

void SerialReceiver::closePort()
{
    if (!serialPort_.isOpen()) {
        return;
    }

    stopReading();
    serialPort_.waitForBytesWritten(200);
    serialPort_.close();
    parser_.reset();

    emit connectionChanged(false);
    emit statusMessage(QStringLiteral("Serial port closed"));
}

bool SerialReceiver::isOpen() const
{
    return serialPort_.isOpen();
}

void SerialReceiver::startReading()
{
    if (!serialPort_.isOpen()) {
        emit serialError(QStringLiteral("Cannot start: serial port is closed"));
        return;
    }

    parser_.reset();
    sendCommand("start_reading\n");
    emit statusMessage(QStringLiteral("Sent start_reading"));
}

void SerialReceiver::stopReading()
{
    if (!serialPort_.isOpen()) {
        return;
    }

    sendCommand("stop_reading\n");
    parser_.reset();
    emit statusMessage(QStringLiteral("Sent stop_reading"));
}

void SerialReceiver::sendCommand(const QByteArray &command)
{
    const qint64 bytesAccepted = serialPort_.write(command);

    if (bytesAccepted == -1) {
        emit serialError(
            QStringLiteral("Serial write failed: %1")
                .arg(serialPort_.errorString()));
    }
}

void SerialReceiver::handleReadyRead()
{
    const QByteArray bytes = serialPort_.readAll();

    static int loggedChunks = 0;

    // if (loggedChunks < 5) {
    //     qInfo() << "[RAW] Chunk size:" << bytes.size()
    //     << "| First bytes:" << bytes.left(16).toHex(' ');

    //     ++loggedChunks;
    // }

    const auto frames = parser_.append(QByteArrayView{bytes});

    for (const auto &frame : frames) {
        const QByteArray payload(
            reinterpret_cast<const char *>(frame.data()),
            static_cast<qsizetype>(frame.size()));

        emit frameReceived(payload);
    }
}

void SerialReceiver::handleSerialError(
    QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    emit serialError(serialPort_.errorString());

    if (error == QSerialPort::ResourceError) {
        serialPort_.close();
        parser_.reset();
        emit connectionChanged(false);
    }
}
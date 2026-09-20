#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <cstdint>

class PressureFrameParser
{
public:
    static constexpr qsizetype Rows = 28;
    static constexpr qsizetype Columns = 56;
    static constexpr qsizetype PayloadSize = Rows * Columns;
    static constexpr qsizetype MarkerSize = 4;
    static constexpr qsizetype FrameSize = PayloadSize + MarkerSize;

    using Frame =
        std::array<std::uint8_t, static_cast<std::size_t>(PayloadSize)>;

    QList<Frame> append(QByteArrayView bytes);

    void reset();

    [[nodiscard]] qsizetype bufferedByteCount() const noexcept;
    [[nodiscard]] quint64 discardedByteCount() const noexcept;
    [[nodiscard]] quint64 parsedFrameCount() const noexcept;

private:
    [[nodiscard]] bool candidateHasValidMarker() const noexcept;
    void limitBufferSize();

    QByteArray buffer_;
    quint64 discardedBytes_ = 0;
    quint64 parsedFrames_ = 0;
};
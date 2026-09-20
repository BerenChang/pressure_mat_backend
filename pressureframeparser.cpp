#include "PressureFrameParser.h"

namespace
{
constexpr std::array<unsigned char, 4> EndMarker{
    0xFF,
    0xFE,
    0xFE,
    0xFF
};
}

QList<PressureFrameParser::Frame>
PressureFrameParser::append(QByteArrayView bytes)
{
    if (!bytes.isEmpty()) {
        buffer_.append(bytes.data(), bytes.size());
    }

    limitBufferSize();

    QList<Frame> completedFrames;

    while (buffer_.size() >= FrameSize) {
        if (!candidateHasValidMarker()) {
            buffer_.remove(0, 1);
            ++discardedBytes_;
            continue;
        }

        Frame frame{};

        for (qsizetype i = 0; i < PayloadSize; ++i) {
            frame[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(buffer_.at(i)));
        }

        completedFrames.append(frame);
        buffer_.remove(0, FrameSize);
        ++parsedFrames_;
    }

    return completedFrames;
}

bool PressureFrameParser::candidateHasValidMarker() const noexcept
{
    if (buffer_.size() < FrameSize) {
        return false;
    }

    for (qsizetype i = 0; i < MarkerSize; ++i) {
        const auto actual =
            static_cast<unsigned char>(buffer_.at(PayloadSize + i));

        if (actual != EndMarker[static_cast<std::size_t>(i)]) {
            return false;
        }
    }

    return true;
}

void PressureFrameParser::limitBufferSize()
{
    constexpr qsizetype MaximumBufferSize = FrameSize * 8;
    constexpr qsizetype RetainedBufferSize = FrameSize * 2;

    if (buffer_.size() <= MaximumBufferSize) {
        return;
    }

    const qsizetype bytesToRemove =
        buffer_.size() - RetainedBufferSize;

    buffer_.remove(0, bytesToRemove);
    discardedBytes_ += static_cast<quint64>(bytesToRemove);
}

void PressureFrameParser::reset()
{
    buffer_.clear();
    discardedBytes_ = 0;
    parsedFrames_ = 0;
}

qsizetype PressureFrameParser::bufferedByteCount() const noexcept
{
    return buffer_.size();
}

quint64 PressureFrameParser::discardedByteCount() const noexcept
{
    return discardedBytes_;
}

quint64 PressureFrameParser::parsedFrameCount() const noexcept
{
    return parsedFrames_;
}
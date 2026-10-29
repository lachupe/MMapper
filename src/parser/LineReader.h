#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

#include <QString>

/// Milliseconds since the epoch, taken once for each chunk or sent line by MumeXmlParser. A
/// reader that counts in seconds uses secondsOf().
using EpochMs = int64_t;

NODISCARD inline constexpr int64_t secondsOf(const EpochMs now)
{
    return now / 1000;
}

/// Why MumeXmlParser asks its readers to forget what they hold. Each reader declares the reasons
/// that reset it (LineReader::resetsOn()); MumeXmlParser fires a reason in one place.
enum class NODISCARD ResetReason : uint8_t {
    /// The connection to MUME closed.
    DISCONNECTED,
    /// MMapper connected again.
    CONNECTED,
    /// The game state is now anything but PLAYING: a rent, a quit, MUME's menu.
    LEFT_GAME,
    /// The game state is now PLAYING.
    ENTERED_GAME,
    /// The player typed a line with echo off (a password): MUME answers it, MMapper did not see it.
    ECHO_OFF,
    /// MUME is not in XML mode (the "/xml" line), so what was read of its markup is void.
    XML_LOST,
};

/// A set of reasons, for declaring which ones reset a reader.
class NODISCARD ResetSet final
{
private:
    uint32_t m_bits = 0;

public:
    constexpr ResetSet() = default;
    constexpr ResetSet(const std::initializer_list<ResetReason> reasons)
    {
        for (const ResetReason reason : reasons) {
            m_bits |= uint32_t{1} << static_cast<unsigned>(reason);
        }
    }
    NODISCARD constexpr bool has(const ResetReason reason) const
    {
        return (m_bits & (uint32_t{1} << static_cast<unsigned>(reason))) != 0;
    }
};

/// One line the player sent to MUME, whoever sent it (MMapper's own client, an alias line, a
/// frontend's MMapper.Input.Command). A string that holds several lines is several commands.
struct NODISCARD SentCommand final
{
    /// The line as sent, without its line ending.
    QString line;
    /// The first word, lowercase, and the words after it, with the spacing simplified.
    QString verb;
    QString args;
    /// When MumeXmlParser took it, once for the whole string.
    EpochMs nowMs = 0;

    SentCommand() = default;
    /// For a reader's tests, and for the one place that makes them.
    SentCommand(const QString &sentLine, const EpochMs now = 0);
    SentCommand(const char *const sentLine, const EpochMs now = 0)
        : SentCommand{QString::fromUtf8(sentLine), now}
    {}
};

/// The lines of a sent string, one for each line the player sent: a final line ending does not
/// make an empty line, and an empty string is the one empty line (Return, which answers a pager).
NODISCARD std::vector<SentCommand> splitSentCommands(const QString &sent, EpochMs now);

/// What every line reader of MumeXmlParser shares: it forgets on the reasons it declares, and
/// takes the player's commands as typed SentCommands. What a reader makes of MUME's lines and
/// prompts differs in kind (a block, an optional, a list), so those stay each reader's own
/// typed receiveLine() and receivePrompt(), and the parser publishes what they return.
class NODISCARD LineReader
{
public:
    LineReader() = default;
    virtual ~LineReader();
    LineReader(const LineReader &) = default;
    LineReader(LineReader &&) = default;
    LineReader &operator=(const LineReader &) = default;
    LineReader &operator=(LineReader &&) = default;

public:
    /// The reasons that reset this reader.
    NODISCARD virtual ResetSet resetsOn() const = 0;
    /// Forgets everything held: no reply gathered, no command waiting.
    virtual void reset() = 0;
    /// One command on its way to MUME. A reader that has no use for commands ignores them.
    virtual void receiveCommand(const SentCommand &) {}
};

/// The readers of MumeXmlParser in one ordered list, so that a reason resets all that declare
/// it and a command reaches all of them.
class NODISCARD LineReaders final
{
private:
    std::vector<LineReader *> m_readers;

public:
    void add(LineReader &reader) { m_readers.push_back(&reader); }
    NODISCARD const std::vector<LineReader *> &all() const { return m_readers; }
    /// Resets, in the order added, each reader that declares `reason`.
    void reset(ResetReason reason);
    /// The command to each reader, in the order added.
    void receiveCommand(const SentCommand &command);
};

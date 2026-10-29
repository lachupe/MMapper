#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/observer/gameobserver.h"
#include "../src/parser/ChunkFrame.h"
#include "../src/parser/TradeLines.h"

/// What MumeXmlParser does with the trade tracker and its chunk frame, for the tests that drive
/// them without a parser: begin the chunk, the line readers, the prompt readers, end the chunk.
class TradeReaders final
{
private:
    GameObserver &m_observer;
    TradeLinesTracker m_tracker;
    ChunkFrame m_frame;
    MudChunkKindEnum m_chunk = MudChunkKindEnum::LINE;

public:
    explicit TradeReaders(GameObserver &observer)
        : m_observer{observer}
        , m_frame{observer}
    {}

    /// False when the line is the answer to a pager, which is no command for any tracker.
    NODISCARD bool receiveCommand(const QString &line)
    {
        if (m_frame.takesPagerAnswer()) {
            return false;
        }
        m_tracker.receiveCommand(SentCommand{line});
        return true;
    }
    NODISCARD MudChunk beginChunk(const bool goAhead,
                                  const bool backspace,
                                  const QString &plain,
                                  const QuietTrafficEnum traffic = QuietTrafficEnum::REPLY)
    {
        MudChunk chunk = m_frame.begin(goAhead, backspace, plain, traffic);
        m_chunk = chunk.kind;
        if (chunk.pager.has_value()) {
            m_tracker.receivePager(*chunk.pager);
        }
        return chunk;
    }
    void receiveLine(const QString &plain)
    {
        publishTradeReplies(m_observer, m_tracker.receiveLine(plain));
    }
    NODISCARD TradeLineKindEnum lastLineKind() const { return m_tracker.lastLineKind(); }
    void receivePrompt()
    {
        if (m_chunk == MudChunkKindEnum::PROMPT) {
            publishTradeReplies(m_observer, m_tracker.receivePrompt());
        }
    }
    void endChunk() { m_frame.end(); }
    NODISCARD bool pagerOpen() const { return m_frame.pagerOpen(); }
    void reset()
    {
        m_tracker.reset();
        m_frame.reset();
    }
    void receiveQuietCommand(const QuietCommandEnum what) { m_frame.receiveQuietCommand(what); }
    NODISCARD bool quietOpen() const { return m_frame.quietOpen(); }
    NODISCARD bool captureQuietLine(const QString &plain)
    {
        return m_frame.captureQuietLine(plain);
    }
};

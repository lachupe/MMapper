// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TradeLines.h"

#include "../observer/gameobserver.h"

// Stub bodies: the interface is committed first so that the trade operations can be built
// against it; the readers follow.

std::optional<PagerLine> parsePagerLine(const QString & /*plainChunk*/)
{
    return std::nullopt;
}

bool stripPagerPrefix(QString & /*line*/)
{
    return false;
}

std::optional<Money> parseMoney(const QString & /*words*/)
{
    return std::nullopt;
}

std::optional<Money> findMoney(const QString & /*sentence*/)
{
    return std::nullopt;
}

void TradeReplies::append(TradeReplies &&other)
{
    auto move = [](auto &to, auto &from) {
        for (auto &x : from) {
            to.push_back(std::move(x));
        }
    };
    move(lists, other.lists);
    move(deals, other.deals);
    move(teachers, other.teachers);
    move(practised, other.practised);
    move(skills, other.skills);
    move(inns, other.inns);
    move(trophies, other.trophies);
}

void TradeLinesTracker::receiveCommand(const QString & /*line*/) {}

TradeReplies TradeLinesTracker::receiveLine(const QString & /*line*/)
{
    return {};
}

void TradeLinesTracker::receivePager(const PagerLine & /*pager*/) {}

TradeReplies TradeLinesTracker::receivePrompt()
{
    return {};
}

void TradeLinesTracker::reset() {}

MudChunk classifyMudChunk(const bool goAhead, const bool backspace, const QString &plain)
{
    MudChunk chunk;
    chunk.plain = plain;
    chunk.kind = backspace ? MudChunkKindEnum::TWIDDLER
                 : goAhead ? MudChunkKindEnum::PROMPT
                           : MudChunkKindEnum::LINE;
    return chunk;
}

TradeReaders::TradeReaders(GameObserver &observer)
    : m_observer{observer}
{}

bool TradeReaders::receiveCommand(const QString & /*line*/)
{
    return true;
}

MudChunk TradeReaders::beginChunk(const bool goAhead, const bool backspace, const QString &plain)
{
    MudChunk chunk = classifyMudChunk(goAhead, backspace, plain);
    m_chunk = chunk.kind;
    return chunk;
}

void TradeReaders::receiveLine(const QString & /*plain*/) {}

void TradeReaders::receivePrompt() {}

void TradeReaders::endChunk()
{
    if (m_chunk == MudChunkKindEnum::PROMPT) {
        m_observer.observeRealPrompt();
    }
}

void TradeReaders::reset() {}

void TradeReaders::publish(const TradeReplies & /*replies*/) {}

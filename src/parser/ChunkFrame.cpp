// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ChunkFrame.h"

#include "../observer/gameobserver.h"

#include <utility>

MudChunk classifyMudChunk(const bool goAhead, const bool backspace, const QString &plain)
{
    MudChunk chunk;
    chunk.plain = plain;
    if (backspace) {
        chunk.kind = MudChunkKindEnum::TWIDDLER;
        return chunk;
    }
    if (goAhead) {
        chunk.pager = parsePagerLine(plain);
        chunk.kind = chunk.pager.has_value() ? MudChunkKindEnum::PAGER : MudChunkKindEnum::PROMPT;
        return chunk;
    }
    chunk.kind = MudChunkKindEnum::LINE;
    qsizetype length = 0;
    if (std::optional<PagerLine> pager = matchPager(plain, length)) {
        if (stripPagerPrefix(chunk.plain)) {
            chunk.pager = std::move(pager);
        }
    }
    return chunk;
}

ChunkFrame::ChunkFrame(GameObserver &observer)
    : m_observer{observer}
{}

bool ChunkFrame::takesPagerAnswer()
{
    return std::exchange(m_pagerOpen, false);
}

MudChunk ChunkFrame::begin(const bool goAhead,
                           const bool backspace,
                           const QString &plain,
                           const QuietTrafficEnum traffic)
{
    MudChunk chunk = classifyMudChunk(goAhead, backspace, plain);
    m_chunk = chunk.kind;
    m_quietEnded = false;
    // Before the pager is published, which says whether it was hidden.
    if (m_quiet.isOpen()) {
        hideQuietChunk(chunk, traffic);
    }
    if (chunk.pager.has_value()) {
        // A pager glued to a line was answered before the line came.
        m_pagerOpen = chunk.kind == MudChunkKindEnum::PAGER;
        m_observer.observePager(*chunk.pager);
    } else if (chunk.kind == MudChunkKindEnum::PROMPT) {
        m_pagerOpen = false;
    }
    return chunk;
}

void ChunkFrame::hideQuietChunk(MudChunk &chunk, const QuietTrafficEnum traffic)
{
    switch (chunk.kind) {
    case MudChunkKindEnum::LINE: {
        // A chunk with nothing in it for the user (tags alone) is no line for any reader.
        if (chunk.plain.isEmpty()) {
            break;
        }
        // A pager glued to the front of the line came before it, and goes with it.
        const bool pagerHidden = chunk.pager.has_value() && m_quiet.receivePager().hidden;
        const QuietCapture::Verdict verdict = m_quiet.receiveLine(chunk.plain, traffic);
        chunk.hidden = verdict.hidden;
        chunk.captured = verdict.captured;
        if (chunk.pager.has_value()) {
            chunk.pager->hidden = pagerHidden && verdict.hidden;
        }
        break;
    }
    case MudChunkKindEnum::PAGER: {
        const QuietCapture::Verdict verdict = m_quiet.receivePager();
        chunk.hidden = verdict.hidden;
        chunk.pager->hidden = verdict.hidden;
        break;
    }
    case MudChunkKindEnum::PROMPT: {
        const QuietCapture::Verdict verdict = m_quiet.receivePrompt();
        chunk.hidden = verdict.hidden;
        m_quietEnded = verdict.ended;
        break;
    }
    case MudChunkKindEnum::TWIDDLER:
        break;
    }
}

bool ChunkFrame::captureQuietLine(const QString &plain)
{
    return m_quiet.isOpen() && m_quiet.receiveLine(plain, QuietTrafficEnum::REPLY).captured;
}

void ChunkFrame::end()
{
    const bool prompt = std::exchange(m_chunk, MudChunkKindEnum::LINE) == MudChunkKindEnum::PROMPT;
    // After every reader's package of this prompt, so that whoever asked finds them published;
    // and before sig2_realPrompt, at which the runner judges.
    if (std::exchange(m_quietEnded, false)) {
        m_observer.observeQuietEnded();
    }
    if (prompt) {
        m_observer.observeRealPrompt();
    }
}

void ChunkFrame::reset()
{
    m_chunk = MudChunkKindEnum::LINE;
    m_pagerOpen = false;
    m_quiet.reset();
    m_quietEnded = false;
}

#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "LineReader.h"
#include "QuietCapture.h"
#include "TradeLines.h"

#include <cstdint>
#include <optional>

#include <QString>

class GameObserver;

/// What MumeXmlParser does with one chunk of MUME's output.
enum class NODISCARD MudChunkKindEnum : uint8_t {
    /// A line ending in a newline.
    LINE,
    /// A chunk on a GA that is the pager line: not a prompt.
    PAGER,
    /// A chunk on a GA that is not the pager: the real prompt.
    PROMPT,
    /// A turn of the spinner (backspaces), neither.
    TWIDDLER
};

struct NODISCARD MudChunk final
{
    MudChunkKindEnum kind = MudChunkKindEnum::LINE;
    /// PAGER, and a LINE that had a pager glued to its front.
    std::optional<PagerLine> pager;
    /// The plain text the line readers get: a glued pager taken off.
    QString plain;
    /// The chunk is of a quiet command's reply and must not be sent to the terminal
    /// (QuietCapture). Every reader still reads it.
    bool hidden = false;
    /// A line of a quiet command's reply, hidden or, where that could not be told, shown.
    bool captured = false;
};

/// Decides once what a chunk is, so that every reader agrees: `goAhead` and `backspace` are
/// how the telnet filter framed it, `plain` its text with colour removed.
NODISCARD MudChunk classifyMudChunk(bool goAhead, bool backspace, const QString &plain);

/// The frame MumeXmlParser puts round each chunk of MUME's output, owned by it: what the chunk is
/// (a line, the pager, the real prompt or a twiddler), the pager's state, the window of a quiet
/// command (QuietCapture), and the promise that at a real prompt every reader's package goes out
/// before sig2_realPrompt.
///
/// MumeXmlParser calls begin() first for every chunk, hands the chunk to its readers, and calls
/// end() last, after everything else it publishes for that chunk; end() of a real prompt emits
/// sig2_quietEnded when the prompt closed a quiet command's window, then sig2_realPrompt. A pager
/// chunk publishes sig2_pager and nothing else, and does not count as a prompt for any reader.
///
/// Since it is here that a chunk is told to be a line, the pager or the prompt, the window of a
/// quiet command is kept here too: begin() says in the chunk whether the terminal is sent it and
/// whether it is of the reply, and marks the pager it hid.
class NODISCARD ChunkFrame final : public LineReader
{
private:
    GameObserver &m_observer;
    MudChunkKindEnum m_chunk = MudChunkKindEnum::LINE;
    bool m_pagerOpen = false;
    QuietCapture m_quiet;
    /// The chunk being read is the prompt that closed the quiet command's window.
    bool m_quietEnded = false;

public:
    explicit ChunkFrame(GameObserver &observer);

    /// A line on its way to MUME. True when it is the answer to a pager ("", "q", "b", ...),
    /// whoever sent it, which must not count as a command for any reader.
    NODISCARD bool takesPagerAnswer();
    /// Classifies the chunk; a pager (a whole chunk, or glued to a line) is published here.
    /// `traffic` is what MUME's markup says the line is, which only a quiet command's window
    /// asks (quietOpen()); see quietTrafficOf().
    NODISCARD MudChunk begin(bool goAhead,
                             bool backspace,
                             const QString &plain,
                             QuietTrafficEnum traffic = QuietTrafficEnum::REPLY);
    /// After everything else for the chunk: sig2_quietEnded, and sig2_realPrompt for a real prompt.
    void end();
    /// Whether the last chunk from MUME left a pager waiting for its answer.
    NODISCARD bool pagerOpen() const { return m_pagerOpen; }

    NODISCARD ResetSet resetsOn() const override
    {
        return {ResetReason::DISCONNECTED, ResetReason::ECHO_OFF, ResetReason::XML_LOST};
    }
    void reset() override;

    /// What the runner says about its quiet command: begun, another line gone out, over.
    void receiveQuietCommand(QuietCommandEnum what) { m_quiet.receiveCommand(what); }
    /// Whether a quiet command's window is open, so that its reply is being kept apart.
    NODISCARD bool quietOpen() const { return m_quiet.isOpen(); }
    /// A line that is kept from the terminal anyway and never becomes a chunk (the MMXP line):
    /// in a quiet command's window it is a line of the reply all the same. True when it is.
    NODISCARD bool captureQuietLine(const QString &plain);

private:
    /// Asks the quiet command's window what to do with the chunk.
    void hideQuietChunk(MudChunk &chunk, QuietTrafficEnum traffic);
};

#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/Consts.h"
#include "../global/LineUtils.h"
#include "../global/macros.h"

#include <cstddef>

#include <QStringView>

// Whether a newline is missing in front of a chunk sent to the user: the text after a prompt
// (MUME sends a prompt without a newline, ended by a go-ahead) or a prompt that would otherwise
// sit at the end of the line before it. Proxy::allocParser() sends the missing "\n" as a chunk of
// its own. Kept apart from the proxy so that tests/TestProxy.cpp can drive it (mume3d FIX.215: every
// client line must start at its start, because the client's Line rules match from there).
class NODISCARD PromptNewlines final
{
private:
    bool m_wasCompact = false;
    bool m_wasPrompt = false;
    size_t m_newlines = 0;

public:
    NODISCARD bool wasPrompt() const { return m_wasPrompt; }
    // Something was sent to the mud, or MUME is no longer prompted: no prompt stands at the end.
    void clearPrompt() { m_wasPrompt = false; }
    // A prompt from the mud begins: was the text in front of it compact (one newline)?
    void notePrompt() { m_wasCompact = (m_newlines == 1); }

    NODISCARD static bool startsWithNewline(QStringView sv)
    {
        if (sv.isEmpty()) {
            return false;
        }
        if (sv.front() == char_consts::C_CARRIAGE_RETURN) {
            sv = sv.mid(1);
        }
        return !sv.empty() && sv.front() == char_consts::C_NEWLINE;
    }

    // The logic may be incomplete; expect more bugs here.
    NODISCARD bool isMissingNewline(const bool isPrompt,
                                    const bool isTwiddler,
                                    const QStringView s) const
    {
        return m_wasPrompt ? (!isTwiddler && !startsWithNewline(s))
                           : (isPrompt && (m_wasCompact ? m_newlines == 0 : m_newlines < 2));
    }

    // After `s` was sent: remember whether it was a prompt, and how many newlines end the text.
    void sent(const bool isPrompt, const bool isTwiddler, const QStringView s)
    {
        m_wasPrompt = isPrompt || isTwiddler;
        if (m_wasPrompt) {
            return;
        }
        mmqt::foreachLine(s, [this](QStringView sv, bool hasNewline) {
            if (!hasNewline) {
                m_newlines = 0;
                return;
            }
            if (!sv.empty() && sv.back() == char_consts::C_CARRIAGE_RETURN) {
                sv.chop(1);
            }
            if (sv.isEmpty()) {
                ++m_newlines;
            } else {
                m_newlines = 1;
            }
        });
    }
};

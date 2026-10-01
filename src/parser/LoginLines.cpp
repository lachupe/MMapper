// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "LoginLines.h"

#include <QStringView>

namespace {

constexpr QStringView NAME_PROMPT = u"by what name do you wish to be known?";

/// Where the last name prompt on `lower` ends, or -1.
NODISCARD qsizetype endOfNamePrompt(const QString &lower)
{
    const qsizetype at = lower.lastIndexOf(NAME_PROMPT);
    return at < 0 ? -1 : at + NAME_PROMPT.size();
}

NODISCARD bool endsInPasswordPrompt(const QString &lower)
{
    // "Account pass phrase:", and older MUME's "Password:", "Character password:" and "Please
    // enter your account password:". The menu's "Pass - Change account pass phrase" has no
    // colon, and is not one.
    return lower.endsWith(u"pass phrase:") || lower.endsWith(u"password:");
}

} // namespace

std::string_view loginPromptKindName(const LoginPromptKindEnum kind)
{
    switch (kind) {
    case LoginPromptKindEnum::NONE:
        break;
    case LoginPromptKindEnum::NAME:
        return "name";
    case LoginPromptKindEnum::PASSWORD:
        return "password";
    }
    return "none";
}

LoginPromptKindEnum parseLoginPromptLine(const QString &line)
{
    const QString lower = line.trimmed().toLower();
    if (lower.isEmpty()) {
        return LoginPromptKindEnum::NONE;
    }
    if (endsInPasswordPrompt(lower)) {
        return LoginPromptKindEnum::PASSWORD;
    }
    // Nothing of MUME's follows the name prompt on its line: what the player types is not in
    // MUME's output.
    if (endOfNamePrompt(lower) == lower.size()) {
        return LoginPromptKindEnum::NAME;
    }
    return LoginPromptKindEnum::NONE;
}

QString parseLoginRefusalLine(const QString &line)
{
    const QString lower = line.trimmed().toLower();
    // "Wrong password.", "Wrong password buddy, try again!"
    if (lower.startsWith(u"wrong password") || lower.startsWith(u"wrong pass phrase")) {
        return QStringLiteral("wrong-password");
    }
    if (lower.startsWith(u"no character or account by that name")
        || lower.startsWith(u"type new if you have no other characters")) {
        return QStringLiteral("no-such-name");
    }
    // "Illegal name, please try another.", "Illegal name, try again."
    if (lower.startsWith(u"illegal name")) {
        return QStringLiteral("illegal-name");
    }
    return QString{};
}

std::optional<LoginPrompt> LoginLinesTracker::receiveLine(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }

    LoginPromptKindEnum kind = parseLoginPromptLine(text);
    // The name prompt run into the pass phrase prompt on one line is a login by itself.
    const qsizetype nameEnd = endOfNamePrompt(text.toLower());
    if (kind == LoginPromptKindEnum::PASSWORD && nameEnd < 0
        && m_prompt.kind == LoginPromptKindEnum::NONE) {
        kind = LoginPromptKindEnum::NONE;
    }

    if (kind != LoginPromptKindEnum::NONE) {
        m_prompt.kind = kind;
        m_prompt.text = (kind == LoginPromptKindEnum::PASSWORD && nameEnd >= 0)
                            ? text.mid(nameEnd).trimmed()
                            : text;
        m_prompt.refusedReason = m_refusedReason;
        m_prompt.refusedText = m_refusedText;
        m_prompt.serial = ++m_serial;
        m_refusedReason.clear();
        m_refusedText.clear();
        return m_prompt;
    }

    if (m_prompt.kind == LoginPromptKindEnum::NONE) {
        return std::nullopt;
    }

    // A refusal waits for the prompt MUME sends the player back to.
    if (const QString reason = parseLoginRefusalLine(text); !reason.isEmpty()) {
        if (m_refusedReason != reason) {
            m_refusedReason = reason;
            m_refusedText = text;
        } else {
            m_refusedText += QLatin1Char(' ') + text;
        }
        return std::nullopt;
    }

    // Anything else: the login went on (the menu, the game) or somewhere not known here.
    reset();
    return LoginPrompt{};
}

void LoginLinesTracker::reset()
{
    m_prompt = LoginPrompt{};
    m_refusedReason.clear();
    m_refusedText.clear();
}

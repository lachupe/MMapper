// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "GameStateLines.h"

#include <QStringView>

std::string_view gameStateName(const GameStateEnum state)
{
    switch (state) {
    case GameStateEnum::UNKNOWN:
        break;
    case GameStateEnum::PLAYING:
        return "playing";
    case GameStateEnum::RENTED:
        return "rented";
    case GameStateEnum::QUIT:
        return "quit";
    case GameStateEnum::MENU:
        return "menu";
    case GameStateEnum::IN_VOID:
        return "void";
    }
    return "unknown";
}

std::optional<GameStateEnum> parseGameStateLine(const QString &line)
{
    const QStringView text = QStringView{line}.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }

    // "Erienal stores your stuff in the safe, and helps you into your chamber." The innkeeper
    // is named, and differs from inn to inn.
    if (text.endsWith(u"stores your stuff in the safe, and helps you into your chamber.")) {
        return GameStateEnum::RENTED;
    }
    // camp rent, for Black Numenoreans; the fire and the cost come before it.
    if (text == u"You finish building your camp and crawl into your tent to rest.") {
        return GameStateEnum::RENTED;
    }
    // quit, and "quit for real" when carrying something.
    if (text == u"Goodbye, friend.. Come back soon!") {
        return GameStateEnum::QUIT;
    }
    // The account menu's prompt. A prompt MUME ends without a telnet GO-AHEAD reaches the parser
    // only with the line after it, so what follows the prompt is allowed for.
    if (text == u"Account>" || text.startsWith(u"Account> ")) {
        return GameStateEnum::MENU;
    }
    // The login prompt, which older MUME went back to after a rent or quit, and which a
    // connection starts at.
    if (text.startsWith(u"By what name do you wish to be known?")) {
        return GameStateEnum::MENU;
    }
    // Idle too long: MUME moves the character out of its room into the void. The line alone, or
    // after a prompt MUME ran into it ("...> "): an emote or speech that quotes it has a name before it.
    if (text == u"You have been idle, and are pulled into a void."
        || text.endsWith(u"> You have been idle, and are pulled into a void.")) {
        return GameStateEnum::IN_VOID;
    }
    return std::nullopt;
}

bool isInGame(const GameStateEnum state)
{
    return state == GameStateEnum::PLAYING || state == GameStateEnum::IN_VOID;
}

GameStateEnum nextGameState(const GameStateEnum current, const GameStateEnum seen)
{
    if (seen == GameStateEnum::MENU
        && (current == GameStateEnum::RENTED || current == GameStateEnum::QUIT)) {
        return current;
    }
    return seen;
}

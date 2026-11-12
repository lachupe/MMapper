#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include <QString>

/// Whether a character is in the game, as far as MUME's output tells.
///
/// Renting, camping and quitting do not close MUME's connection: MUME goes back to its account
/// menu (to its login prompt, before accounts), and nothing in GMCP says so -- Core.Goodbye is
/// only sent before the connection closes, and Char.Name only on login. So the lines that end a
/// session are read here, and GMCP Char.Name and Room.Info say the character is playing again.
/// MMapper.Session.State publishes the result as `game`.
///
/// The sentences are MUME's own, from the powwow logs:
///
///   Erienal stores your stuff in the safe, and helps you into your chamber.     (rent)
///   You finish building your camp and crawl into your tent to rest.            (camp rent)
///   Goodbye, friend.. Come back soon!                                          (quit)
///   Account>                                                                   (account menu)
///   By what name do you wish to be known?                                      (login prompt)
///   You have been idle, and are pulled into a void.                            (idle: the void)
enum class NODISCARD GameStateEnum : uint8_t {
    /// Nothing has been seen yet since MMapper connected to MUME, or it is not connected.
    UNKNOWN,
    /// A character is in the game: Char.Name or Room.Info arrived.
    PLAYING,
    /// The character rented at an inn, or camped (camp rent), and MUME is back at its menu.
    RENTED,
    /// The character quit, and MUME is back at its menu.
    QUIT,
    /// MUME is at its account menu or its login prompt, with no rent or quit seen before it.
    MENU,
    /// The character idled and was pulled into the void: out of its room, still in the game. The next
    /// command sent to MUME brings it back (GameObserver::observeSentToMud()); a Room.Info meanwhile does
    /// not, since MUME may describe the void itself. After a longer idle MUME drops the connection.
    IN_VOID,
};

/// The protocol's name for `state`: "unknown", "playing", "rented", "quit", "menu" or "void".
NODISCARD std::string_view gameStateName(GameStateEnum state);

/// What one line of MUME's output says about the game state, or nothing. `line` is the line as
/// the user sees it, colour removed; a prompt ("Account> ") counts as a line.
NODISCARD std::optional<GameStateEnum> parseGameStateLine(const QString &line);

/// The state after `seen`, given `current`. The menu that follows a rent or a quit keeps that
/// rent or quit, which says more; everything else simply replaces what was before.
NODISCARD GameStateEnum nextGameState(GameStateEnum current, GameStateEnum seen);

/// Whether a character is in the game in `state`: playing, or pulled into the void (which any command
/// ends). Leaving the game -- a rent, a quit, the menu -- is what forgets the character's state; a
/// step between PLAYING and IN_VOID forgets nothing.
NODISCARD bool isInGame(GameStateEnum state);

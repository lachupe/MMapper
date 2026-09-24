#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../clock/mumeclock.h"
#include "../clock/mumemoment.h"
#include "../global/macros.h"
#include "../map/RoomHandle.h"
#include "../parser/CombatLines.h"
#include "../parser/SendToUserSourceEnum.h"
#include "../parser/WeatherLines.h"
#include "../parser/XmlElement.h"
#include "../proxy/GmcpMessage.h"

#include <string_view>

#include <QString>

/// Builders for the MMapper.* GMCP packages published to frontend clients.
///
/// These packages are MMapper's own; they are never sent upstream to MUME. Each one is
/// shaped so that MUME could plausibly define an equivalent one day, per the frontend
/// protocol design rule: describe the fact, not the implementation.
namespace frontend_messages {

/// Stable protocol-level name for the producer of a chunk of terminal text, so that a
/// client can distinguish game output from MMapper's own injected messages.
NODISCARD std::string_view toProtocolString(SendToUserSourceEnum source);

/// MMapper.Terminal.Output — one chunk of downstream terminal text.
///
/// The text is exactly what a telnet client would have received, ANSI escapes included
/// and telnet framing excluded. A chunk is not necessarily a whole line: when goAhead is
/// true the chunk ends at a telnet GO-AHEAD and is a prompt rather than a finished line.
NODISCARD GmcpMessage makeTerminalOutput(SendToUserSourceEnum source,
                                         const QString &text,
                                         bool goAhead);

/// MMapper.Session.State — MMapper's own connection state, distinct from character state.
///
/// `role` is "driving" for the frontend that owns MMapper's single downstream session and
/// may therefore send input, and "observing" for one watching a session another client
/// owns. It is per-connection: two frontends attached at once see different values.
NODISCARD GmcpMessage makeSessionState(bool upstreamConnected,
                                       bool mapLoaded,
                                       bool echo,
                                       bool driving);

/// MMapper.Session.Error — a protocol error reported to one frontend.
///
/// Reported in preference to closing the connection, so that a misbehaving client can be
/// debugged rather than merely disconnected.
NODISCARD GmcpMessage makeError(const QString &code, const QString &message);

/// MMapper.Map.Position — the room MMapper currently believes the player occupies.
///
/// Carries all three room identities, because they are not interchangeable:
///
///   serverId   MUME's own id, from GMCP Room.Info. Globally stable, and the preferred
///              key, but omitted entirely when MUME did not supply one.
///   externalId MMapper's persistent id within the current map file. Always present, and
///              the correct fallback key when serverId is absent.
///   id         A session-local handle. Valid only for this connection; it changes when
///              the map is reloaded, so clients must never persist it.
///
/// The layout block is MMapper's classic map grid. It is one possible arrangement of the
/// room graph, not a metric or canonical 3D embedding: a renderer may use it as a hint,
/// derive its own spatial layout, or ignore it entirely.
NODISCARD GmcpMessage makeMapPosition(const RoomHandle &room);

/// MMapper.Xml.Element -- one element from MUME's XML mode, once it closed.
///
/// MUME marks up its output with tags this parser otherwise strips before anything sees
/// them, and several of them carry facts GMCP has no package for at all: hit, damage, miss
/// and avoid_damage bracket each blow, character/player/enemy/familiar name the people in
/// it, and object marks what is lying in the room.
///
/// `tag` is the name as MUME spelled it, so an element this build does not recognise is
/// still identifiable; `category` is the classification, and is "unknown" for those. Nested
/// elements are inside `children` rather than reported separately, so a blow arrives
/// together with its participants. `text` is what MUME would have shown, children included.
NODISCARD GmcpMessage makeXmlElement(const XmlElement &element);

/// MMapper.Combat.Event -- one event in a fight, read off a line of MUME's text.
///
/// GMCP carries the state of a fight -- Char.Vitals names the opponent, Room.Chars says who
/// fights whom -- but none of its events, which are read off MUME's prose here. The XML
/// elements that bracket a blow still go out as MMapper.Xml.Element (above), but it is this
/// package that says where the blow landed. `kind` is blow, flee, refused, bash, backstab,
/// condition, death, cast or self. People are named as MUME wrote
/// them, "you" for the player, with group labels removed. Only the fields a kind uses are
/// sent: a blow has `outcome` (hit, parry, dodge, miss), `verb`, and for a hit `part`,
/// `quality`, `severity` and `effect`; flee, cast and self have a `phase`; flee's direction,
/// a condition, and a spell's uttered words are in `detail`. `text` is the line itself.
NODISCARD GmcpMessage makeCombatEvent(const CombatEvent &event);

/// MMapper.Time.State -- the game clock as MMapper knows it.
///
/// MUME publishes no clock over GMCP: it announces the sun's changes (Event.Sun) and the
/// moon's (Event.Moon), and the rest is MMapper's MumeClock, synchronised from the `time`
/// command, MSSP and those events. A renderer needs more than the four phases -- where the
/// sun stands between dawn and dusk, how bright the moon is -- so the whole moment goes out.
///
/// `month` and `day` count from 1. `precision` is MMapper's own confidence: "unset" and
/// "day" mean the hour is only a guess and should not move a sun, "hour" and "minute" that
/// it can. The moon is MMapper's model of it; MUME itself sends no phase. `weekday` is
/// MMapper's computation, which MUME's own calendar help does not agree with.
///
/// Published when the hour changes, when the precision changes and when the clock is set
/// again, so a frontend runs its own clock in between at `secondsPerHour`.
NODISCARD GmcpMessage makeTimeState(const MumeMoment &moment, MumeClockPrecisionEnum precision);

/// MMapper.Weather.Event -- one of MUME's weather or terrain lines, read.
///
/// GMCP gives the sky's state as the prompt's symbols (Char.Vitals) and the sun, the moon and
/// the Darkness as Event.*; the rest of what MUME says about the weather is prose, which
/// WeatherLines reads. `kind` is lightning (seen), thunder (heard), fog, storm, snow, frost,
/// ice, darkness or unknown. Only the fields a kind uses are sent: `level` (the state, or the
/// state a change is heading for when `changing` is true; "none" for lightning that stopped),
/// `direction` for fog drifting in and for a storm seen or heard elsewhere, `precipitation`
/// for a storm, and `magic` for weather someone changed with a spell. `text` is always the
/// line itself. An event, so it is not replayed.
NODISCARD GmcpMessage makeWeatherEvent(const WeatherLine &line);

/// MMapper.Weather.Ground -- snow lying, frost and ice where the body stands.
///
/// Complete at the prompt that ends each room display, which follows MMapper moving the player,
/// so `room` is the room MMapper.Map.Position has just named, with the same identities, and
/// is omitted when there is no current room. `snow` is none, some, lot or deep; `frost` none,
/// slight, frosty, very or frozen; `ice` none, film, some, thick or frozen; each is "unknown"
/// when the display could not say (dense fog, darkness). `entered` is true for a room display
/// and false for a change of the ground where the character stands. State, so the latest is
/// replayed to a frontend that subscribes later.
NODISCARD GmcpMessage makeGroundState(const GroundState &state, const RoomHandle *room);

} // namespace frontend_messages

#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../map/RoomHandle.h"
#include "../parser/SendToUserSourceEnum.h"
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

} // namespace frontend_messages

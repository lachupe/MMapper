#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../clock/mumeclock.h"
#include "../clock/mumemoment.h"
#include "../global/macros.h"
#include "../map/RoomHandle.h"
#include "../parser/AccountLines.h"
#include "../parser/CharAffects.h"
#include "../parser/CharFollowers.h"
#include "../parser/CharRefused.h"
#include "../parser/ExitLooks.h"
#include "../parser/RoomDoors.h"
#include "../parser/CharLines.h"
#include "../parser/CombatLines.h"
#include "../parser/ContainerLines.h"
#include "../parser/GameStateLines.h"
#include "../parser/ItemLines.h"
#include "../parser/RoomContents.h"
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

NODISCARD GmcpMessage makeCharCommand(const ItemCommandObservation &command);

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

/// The map MMapper has loaded, as MMapper.Session.State names it.
struct NODISCARD MapIdentity final
{
    /// The map file's name without its directory, or empty for a map never loaded or saved.
    QString name;
    /// How many rooms it has.
    qint64 rooms = 0;
    /// 0 when the map was loaded (or a new one started), and one more for each change since.
    qint64 generation = 0;
};

/// MMapper.Session.State — MMapper's own connection state, distinct from character state.
///
/// `role` is "driving" for the frontend that owns MMapper's single downstream session and
/// may therefore send input, and "observing" for one watching a session another client
/// owns. It is per-connection: two frontends attached at once see different values.
///
/// `mapName`, `mapRooms` and `mapGeneration` name the loaded map, so that a frontend reading
/// its own export of a map can notice when that is not the map MMapper is using, or no longer
/// matches it; `mapLoaded` is true when the map has any rooms.
///
/// `game` says whether a character is in the game: "playing", "rented" (rent or camp rent),
/// "quit", "menu" (MUME's account menu or login prompt, no rent or quit seen before it) or
/// "unknown" (nothing seen yet, or MUME not connected). MUME goes back to its menu after a rent
/// or a quit with the connection open, so `upstream` alone cannot say the character has gone.
///
/// `trade` is the version of the trade operations MMapper.Trade.Request runs (TradeOperations),
/// 1 today. `viewer` is MUME's `change viewer` setting as far as MMapper knows it: "external",
/// "simple", "off" or "unknown" (TradeOperations::viewerState()).
NODISCARD GmcpMessage makeSessionState(bool upstreamConnected,
                                       const MapIdentity &map,
                                       bool echo,
                                       bool driving,
                                       GameStateEnum game = GameStateEnum::UNKNOWN,
                                       const QString &viewer = QStringLiteral("unknown"));

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
/// `fingerprint` is a hash of what the room shows (see RoomFingerprint.h), the same as the
/// `fingerprint` attribute the XML export writes on the room, so that a frontend can find the
/// room in its own export when serverId is absent and the export's ids differ from MMapper's.
///
/// `ridable` is what MMapper's map says of riding into the room: true, false, or null when the
/// map does not say. `exits` has one entry per exit that leads to a room of the map, keyed by
/// the side's letter (n e s w u d), with that room's `externalId`, its `serverId` when MUME
/// gave one, and its `ridable`, so that a frontend can warn before a ride MUME would refuse. An
/// exit to several rooms names the one with the lowest externalId, as the fingerprint does. No
/// door's name is in it.
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

/// MMapper.Room.Contents -- the objects lying in the room where the body stands.
///
/// MUME has no GMCP package for what lies in a room; its room display lists it after the
/// description, mixed with the people there. MMapper takes those lines and leaves out the ones
/// Room.Chars names as people. Complete at the prompt that ends each room display, which
/// follows MMapper moving the player, so `serverId` and `externalId` name the room the latest
/// MMapper.Map.Position did; both are omitted when there is no current room, and `serverId`
/// when MUME gave none. Each object has its `index` in MUME's list (the order "2.chest" counts
/// in), the `line`, the `name` of an <object> element in it or null, `count`, whether MMapper
/// takes it for a `container`, and for a container its `keyword` and `target`, the word a
/// command should use for it now. `state` says what replies have shown of it: `open`,
/// `locked`, `pickproof` and `empty` are true or false, or null while unknown, and `known` is
/// when the latest evidence arrived, in Unix seconds, 0 for none. `seen` is false when the
/// display could not show the room; `entered` is false when this was sent again because a
/// container's state changed. State, so the latest is replayed to a frontend that subscribes.
NODISCARD GmcpMessage makeRoomContents(const RoomContentsSnapshot &contents, const RoomHandle *room);

/// MMapper.Room.Container -- one reply to a container command, paired with the command.
///
/// `target` is what the player named ("chest", "2.chest"), `action` one of open, close,
/// unlock, lock, pick, look, get or put, and `result` what the reply said: opened, closed,
/// already-open, already-closed, locked, unlocked, no-key, key-broke, picking, picked,
/// pick-failed, pick-stopped, pickproof, empty, contents, not-found or cannot. `items` lists
/// what a look showed inside, or what was taken out or put in, each with a `name`, a `count`
/// and MUME's own `text`; it is omitted when there are none. `index` is the object in
/// MMapper.Room.Contents the command reached, omitted when that could not be told. `text` is
/// the reply itself. An event, so it is not replayed.
NODISCARD GmcpMessage makeContainerEvent(const ContainerEvent &event);

/// MMapper.Char.Equipment -- what someone wears and wields, from "You are using:" or
/// "<someone> is using:".
///
/// MUME has no GMCP package for items; its listings are text, read by ItemBlockTracker. `owner`
/// is "you" for the player's own, which is state and replayed, and otherwise the person as MUME
/// named them, group label taken off, which is an event (a look at someone). Each of `items` has
/// the `slot` id its label maps to (see equipmentSlot), the `label` in MUME's words, the `name`,
/// `count`, `condition` (null when none is given) and `flags` of the item, `twoHanded` when it is
/// wielded in both hands, and the line as `text`. Repeated slots keep MUME's order.
NODISCARD GmcpMessage makeCharEquipment(const ItemBlock &block);

/// MMapper.Char.Inventory -- what someone carries, from "You are carrying:".
///
/// Items as in MMapper.Char.Equipment, without slot and label. `owner` is "you", and `peek` is
/// false, for the player's own inventory, which is state and replayed. A thief's "You attempt to
/// peek at the inventory:" gives `peek` true and the owner of the equipment shown just before it
/// in the same reply, or null; that is an event.
NODISCARD GmcpMessage makeCharInventory(const ItemBlock &block);

/// MMapper.Char.Container -- what is in one container, from "<keyword> (used|carried|here) :".
///
/// `keyword` is MUME's own first keyword for it, `where` is used (worn), carried or here (in the
/// room), or null when the reply did not say, and `closed` is true for a look answered "It is
/// closed.", which has no items. State per container; the server keeps the last few.
NODISCARD GmcpMessage makeCharContainer(const ItemBlock &block);

/// MMapper.Char.Item -- one line that changed, or refused to change, what the player wears or
/// carries.
///
/// `action` is wear, remove, wield, hold, light, get, put, drop, give, receive or refused, and
/// `text` the line. The rest is sent only when the line says it: `item` as MUME named it,
/// `container` it came out of or went into, `place` on the body in MUME's words and its `slot`
/// id, the `other` person given to or received from, and for refused the `reason`: slot-taken,
/// hands-full, two-hands, too-many, too-heavy, cursed, wont-fit, not-carried, not-worn or
/// cannot. An event, so it is not replayed.
NODISCARD GmcpMessage makeCharItem(const ItemEvent &event);

/// MMapper.Char.Stat -- MUME's reply to `stat`.
///
/// Numbers are JSON numbers, and a figure the reply did not state is left out: `ob`, `db`,
/// `pb`, `armour` (percent; 0 for "Armour: none."), `wimpy`, `neededXp`, `neededTp` (both
/// absent at the highest level), `gold`, and `wp` in old wordings. `mood` and `alert` are
/// MUME's words, lowercase; `condition` lists "hungry", "thirsty", "drunk" when the line says
/// any. `affects` and `wounds` are always sent: the names after "Affected by:" and the wounds
/// among them ("a light wound at the head (clean)"), empty when the reply listed none. `text` is
/// the reply. State: replayed to a frontend that connects later.
NODISCARD GmcpMessage makeCharStat(const CharStat &stat);

/// MMapper.Char.Score -- MUME's reply to `score` (`reply` "score": the pools only) or to `info`
/// (`reply` "info": the whole sheet).
///
/// Every figure is left out unless the reply stated it: `abilities` (an object of `str`, `int`,
/// `wis`, `dex`, `con`, `wil`, `per`), `ob`, `db`, `pb`, `armour` (0 for "You are not wearing
/// any armour."), `hp`, `maxhp`, `mana`, `maxmana`, `mp`, `maxmp`, `mood`, `wimpy` (0 for "You
/// will fight to the death."), `xp`, `tp`, `renown` (the line less its "(N wp)") and `wp`,
/// `neededXp`, `neededTp`, `gold`, `silver`, `copper`, `language`, and `swim` and `climb` as
/// MUME's sentences. `effects` and `wounds` are sent when the sheet was complete: empty when it
/// had no effects list. State: the fields are merged for a frontend that connects later, so a
/// one-line `score` updates the pools of the last sheet rather than replacing it.
NODISCARD GmcpMessage makeCharScore(const CharScore &score);

/// MMapper.Char.Burden -- "Your equipment weighs one hundred fourteen pounds. Heavy, but we will
/// manage..." from `info`: `pounds` as a number (0 for "nothing"), `word` MUME's comment when
/// there is one, and `text` the line. State: replayed.
NODISCARD GmcpMessage makeCharBurden(const CharBurden &burden);

/// MMapper.Char.Level -- the reply to CHAR_LEVEL_REQUEST (`info MMXP %l %x %X %t %T`), which a
/// frontend sends itself: `level`, `xp`, `neededXp` (experience still needed for the next
/// level), `tp` and `neededTp` (travel points still needed; a character levels only when both
/// are met), and `text` the line as MUME sent it. A figure MUME did not print as a number is
/// left out. The line is not shown in the terminal. State: replayed as last sent.
NODISCARD GmcpMessage makeCharLevel(const CharLevel &level);

/// MMapper.Char.Wimpy -- the hit points below which the character flees, as MUME last stated
/// them: `{"wimpy": 120}`, 0 for none. From the reply to `change wimpy` ("Wimpy set to: 120"),
/// and again after each `stat` and `info` that states it. State: replayed as last sent.
NODISCARD GmcpMessage makeCharWimpy(const CharWimpy &wimpy);

/// MMapper.Char.Affects -- the lasting effects on the player's character, whole, at each
/// change: `{"affects": [{"name": "armour", "since": 1790842000, "refreshed": 1790842600,
/// "source": "line"}, {"name": "noquit", "source": "stat"}]}`. `since` and `refreshed` are unix
/// seconds and left out when not known; `source` is "line" when one of MUME's lines told of the
/// effect, "stat" when only `stat`'s list did. State: replayed as last sent. See
/// CharAffectsTracker.
NODISCARD GmcpMessage makeCharAffects(const std::vector<CharAffect> &affects);

/// MMapper.Char.Followers -- the creatures that follow the player's character and take its
/// orders, whole, at each change: `{"followers": [{"name": "a mother eagle", "label": "one",
/// "kind": "charmie", "here": true, "state": "following", "since": 1790842000, "lastOrder":
/// "assist", "lastRefused": "ride donk"}], "reply": {"order": "assist", "who": "followers",
/// "result": "failed", "failed": ["a mother eagle"]}}`. `kind` is charmie, mount, summoned or
/// unknown; `state` following, refusing, lost, left or dead, the last two sent once and the
/// follower in no message after; `label` is "" when none is known; `since` (unix seconds),
/// `lastOrder` and `lastRefused` are left out when not known. `reply` is there only in the
/// message an `order`'s answer caused: `result` ok, failed, none-here, syntax or asleep.
/// With them, the other side of following: `"following": "Grayelf"` (whom the character
/// follows; left out when nobody), `"leader": {"name": "Grayelf", "you": false}` or `{"you":
/// true}` when the character follows nobody and a player or a bound follower follows it (left
/// out when nobody is known to lead), `"players": ["Budach"]` (the players that follow the
/// character; always there) and `"protect": {"protecting": ["Kazadoe"]}` (whom the character
/// said it will try to protect; left out until a line stated it). State:
/// replayed as lastingFollowers() of the last sent -- without `reply` and without those that
/// left or died. See CharFollowersTracker.
NODISCARD GmcpMessage makeCharFollowers(const CharFollowers &followers);

/// MMapper.Char.Refused -- a command MUME refused, read off its sentence: `text` (the line),
/// `reason` (a stable code: already, fighting, not-fighting, position, slept, self, queued, busy,
/// no-target, no-space, afraid, silenced, no-skill, no-item, noride, no-control, unwilling,
/// riding, guarded, door-moving, door-iced, door-blocked, door-locked, dark, water, not-here,
/// unknown), and where the line or the command says them `action` (the frontend's action: move,
/// flee, bash, cast, open ...), `target` (who or what the line names) and `dir` (the side of a
/// move or a door, one letter). An event: not replayed. The refusals that are already
/// MMapper.Combat.Event `refused`, MMapper.Char.Followers `reply`, MMapper.Guild.Practised or
/// MMapper.Room.Container are not sent again here, except a ride refused. See CharRefused.
NODISCARD GmcpMessage makeCharRefused(const CharRefused &refused);

/// MMapper.Room.Door -- the doors of the room the character stands in, as MUME told the player
/// on this visit: `doors`, each {`dir` (one letter; left out when not known), `name` (the
/// door's keyword; left out when MUME has not shown it), `state` (open, closed, locked, iced,
/// blocked, broken, molten, unknown), `since` (unix seconds of the state's last change)}, and
/// `room`, MUME's id of the room, left out when it gave none. Never from MMapper's map. State:
/// whole at each change, replayed as last sent, and empty again in another room. See
/// RoomDoorTracker.
NODISCARD GmcpMessage makeRoomDoor(const RoomDoors &doors);

/// MMapper.Room.Look -- MUME's answer to a look at a side, paired with the look whichever client
/// sent it: `room` (MUME's id of the room it was sent in; left out when Room.Info gave none),
/// `dir` (north, east, south, west, up, down), `command` (the line as sent), `kind`
/// (description, nothing, door, dark, no-exit, empty, other), `text` (the answer, its lines
/// joined with "\n", MUME's own asynchronous lines left out), `lines`, `door` {`name`, `state`
/// (open, closed, broken)} when the answer named one, `uncertain` and `reasons` (extra-prompt,
/// fight, room-display, moved, empty, other, lost), `dropped` (the lines left out of `text`) and
/// `raw` (every line from the look to the prompt that closed it). An event: not replayed. Sent
/// to every subscriber of MMapper.Room, driving or observing. See ExitLookTracker.
NODISCARD GmcpMessage makeRoomLook(const ExitLook &look);

/// MMapper.Account.Menu -- MUME's account menu: `commands`, each {`name` (the command word,
/// lowercase), `usage` ("Play <name>"), `help`}, in the menu's order, and `sorts`, the sort
/// orders `list` takes where the menu names them. State: replayed as last sent, and kept when
/// the game state changes (it is what MUME's menu offers while no character plays).
NODISCARD GmcpMessage makeAccountMenu(const AccountMenu &menu);

/// MMapper.Account.Chars -- MUME's reply to `list [<sort>]`: `account` and `chars`, each
/// {`name`, `race`, `lvl`, `class`, `level`, `logon`, `playing`, `area`, `rent`, `delete`}, in
/// the reply's order. The host column is never sent. An empty column is left out. State:
/// replayed as last sent, and kept when the game state changes.
NODISCARD GmcpMessage makeAccountChars(const AccountChars &chars);

/// MMapper.Account.Reply -- a one-line answer of the menu: `kind` "wait" (`seconds` before the
/// character may log in) or "unknown" (`command` MUME did not know), and `text`. Event.
NODISCARD GmcpMessage makeAccountReply(const AccountReply &reply);

} // namespace frontend_messages

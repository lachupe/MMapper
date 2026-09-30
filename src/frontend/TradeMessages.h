#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../parser/TradeLines.h"
#include "../proxy/GmcpMessage.h"

#include <string_view>

/// Builders for the MMapper.* packages of MUME's shops, guilds, inns, trophies and viewer, read
/// by TradeLinesTracker. Field names are camelCase as in the other MMapper.* packages; a figure
/// the reply did not state is left out, never sent as zero, and `text` is always the reply as
/// MUME wrote it, one line per line.
namespace frontend_messages {

/// "buy", "sell", "value", "miss", "closed", "refused".
NODISCARD std::string_view toProtocolString(ShopDealKindEnum kind);

/// MMapper.Shop.List -- MUME's reply to `list [filter]`: `keeper` (empty: the list does not name
/// one), `query` (the filter of the `list` command it answers), `rows` of {`number`, `count`,
/// `name` (MUME's, plural), `singular`, `condition`, `age`, `priceCopper`, `priceText`,
/// `group`}, `empty` true for "There are no such things for sale.", `paged` and `complete`. An
/// event, not replayed.
NODISCARD GmcpMessage makeShopList(const ShopList &list);

/// MMapper.Shop.Deal -- one exchange with a keeper: `kind`, `keeper`, `amountCopper`,
/// `amountText`, `items`, `said` (the tell) and `text`. An event, not replayed.
NODISCARD GmcpMessage makeShopDeal(const ShopDeal &deal);

/// MMapper.Guild.Teacher -- a guildmaster's table: `teacher`, `kind` ("spells" or "skills"),
/// `sessionsLeft`, `rows` of {`name`, `used`, `most`, `knowledgePct`, `difficulty`, `advice`},
/// `paged`, `complete`. An event, not replayed.
NODISCARD GmcpMessage makeGuildTeacher(const GuildTeacher &teacher);

/// MMapper.Guild.Practised -- one `prac <name>` answered: `name`, `used`, `most`,
/// `knowledgePct`; or `refused` with MUME's sentence. An event, not replayed.
NODISCARD GmcpMessage makeGuildPractised(const GuildPractised &practised);

/// MMapper.Char.Skills -- the general practice table: `sessionsLeft`, `rows` of {`name`,
/// `knowledge`, `trained`, `difficulty`, `class`, `mana`, `casting`}, `paged`, `complete`.
/// State: replayed as last sent.
NODISCARD GmcpMessage makeCharSkills(const CharSkills &skills);

/// MMapper.Inn.Offer -- the innkeeper's quote: `keeper`, `perDayCopper`, `perDayText`,
/// `lastsText`, `confiscated`, `said`, `retireAsksRepeat`, `text`. An event, not replayed.
NODISCARD GmcpMessage makeInnOffer(const InnOffer &offer);

/// MMapper.Char.Trophies -- `trop`: `rows` of {`name`, `kills`, `knowledgePct`, `player`},
/// `totalKills`, `distinct`, `paged`, `complete`. An event, not replayed.
NODISCARD GmcpMessage makeCharTrophies(const CharTrophies &trophies);

/// MMapper.View.Text -- a text MUME showed through its viewer: `title`, `text`. An event, not
/// replayed.
NODISCARD GmcpMessage makeViewText(const ViewText &view);

} // namespace frontend_messages

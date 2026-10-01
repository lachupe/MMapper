#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2023 The MMapper Authors
// Author: Mike Repass <mike.repass@gmail.com> (Taryn)

#include "../clock/mumemoment.h"
#include "../global/Signal2.h"
#include "../map/PromptFlags.h"
#include "../parser/AccountLines.h"
#include "../parser/CharFollowers.h"
#include "../parser/CharLines.h"
#include "../parser/CombatLines.h"
#include "../parser/ContainerLines.h"
#include "../parser/GameStateLines.h"
#include "../parser/ItemLines.h"
#include "../parser/RoomContents.h"
#include "../parser/SendToUserSourceEnum.h"
#include "../parser/TradeLines.h"
#include "../parser/WeatherLines.h"
#include "../parser/XmlElement.h"
#include "../proxy/GmcpMessage.h"

#include <QString>

/// A chunk of downstream terminal output, exactly as it was handed to UserTelnet.
///
/// Unlike GameObserver::sig2_sentToUserString, the text retains its ANSI escapes and
/// carries the metadata a terminal needs in order to reproduce the stream faithfully.
struct NODISCARD TerminalOutput final
{
    /// Who produced this chunk: MUME, or one of MMapper's own injectors.
    SendToUserSourceEnum source = SendToUserSourceEnum::FromMud;
    /// Text with ANSI escapes intact; not necessarily a whole line.
    QString text;
    /// True when the chunk ends at a telnet GO-AHEAD, i.e. it is a prompt or twiddler
    /// rather than a newline-terminated line.
    bool goAhead = false;
};

class NODISCARD GameObserver final
{
public:
    Signal2<> sig2_connected;
    Signal2<> sig2_disconnected;

    Signal2<QString> sig2_sentToMudString;  // removes ANSI
    Signal2<QString> sig2_sentToUserString; // removes ANSI

    // Same stream as sig2_sentToUserString, but with ANSI and framing metadata intact.
    Signal2<TerminalOutput> sig2_sentToUserTerminal;

    Signal2<GmcpMessage> sig2_sentToUserGmcp;

    /// One complete element from MUME's XML mode, once its closing tag arrived.
    ///
    /// The parser strips these tags before anything downstream sees the text, so this is
    /// the only place the markup survives. It carries what GMCP has no package for: which
    /// blow landed on whom, what is lying in the room, and who spoke. Emitted after the
    /// terminal output of the line that closed the element.
    Signal2<XmlElement> sig2_sentToUserXml;

    /// One event in a fight -- a blow, a flee, a bash, a cast starting or breaking -- read off
    /// a line of MUME's output. GMCP carries the state of a fight but none of its events, so
    /// these are recognised once, here, instead of by every frontend. Emitted after the line's
    /// own terminal output.
    Signal2<CombatEvent> sig2_sentToUserCombat;

    /// One of MUME's weather or terrain lines, as WeatherLines reads it: lightning seen, thunder
    /// heard, fog drifting in, frost, ice or snow on the ground, a storm, weather magic, the
    /// Necromancer's Darkness. Only the prompt's symbols carry the sky's state in GMCP; the rest
    /// is prose, recognised here once. Emitted after the XML element it came in.
    Signal2<WeatherLine> sig2_weatherLine;
    /// The ground where the body stands -- snow lying, frost, ice on water -- complete at each
    /// prompt that ends a room display or a change of the ground. See GroundTracker.
    Signal2<GroundState> sig2_groundChanged;
    /// The objects lying in the room where the body stands, from the room display's lines less
    /// the people Room.Chars names, with what is known of each container. Complete at the
    /// prompt that ends a room display, and again at the prompt after a reply that changed what
    /// is known of a container there. See RoomContentsTracker.
    Signal2<RoomContentsSnapshot> sig2_roomContents;
    /// One reply to a container command, paired with the command that caused it: a chest
    /// opened, found locked, picked, looked into. See ContainerTracker.
    Signal2<ContainerEvent> sig2_containerEvent;
    /// A listing of what someone wears or carries, or of a container, complete: "You are
    /// using:", "You are carrying:", "backpack (used) :", "Miltar is using:". See
    /// ItemBlockTracker.
    Signal2<ItemBlock> sig2_itemBlock;
    /// One line that changed, or refused to change, what the player wears or carries: "You
    /// fasten a sable pouch on your belt." See parseItemEvent.
    Signal2<ItemEvent> sig2_itemEvent;
    Signal2<ItemCommandObservation> sig2_itemCommand;
    /// MUME's replies to `stat`, `score` and `info`, and the burden line of `info`, complete.
    /// See CharLinesTracker.
    Signal2<CharStat> sig2_charStat;
    Signal2<CharScore> sig2_charScore;
    Signal2<CharBurden> sig2_charBurden;
    /// The wimpy as MUME last stated it: "Wimpy set to: 120", or a `stat` or `info` saying it.
    Signal2<CharWimpy> sig2_charWimpy;
    /// The followers of the player's character after a change -- a bond made or ended, one left
    /// behind or back, an order answered. See CharFollowersTracker.
    Signal2<CharFollowers> sig2_charFollowers;
    /// The reply to CHAR_LEVEL_REQUEST, which is kept out of the terminal. See parseCharLevelLine.
    Signal2<CharLevel> sig2_charLevel;
    /// MUME's account menu, its `list` of characters and its one-line answers. See
    /// AccountLinesTracker.
    Signal2<AccountMenu> sig2_accountMenu;
    Signal2<AccountChars> sig2_accountChars;
    Signal2<AccountReply> sig2_accountReply;
    /// MUME's replies at shops, guilds and inns, and to `trop`, complete. See TradeLinesTracker.
    /// The tables and deals come at the real prompt that ends them, before sig2_realPrompt; a
    /// practised line at once, an inn quote once its "You have enough money" line came.
    Signal2<ShopList> sig2_shopList;
    Signal2<ShopDeal> sig2_shopDeal;
    Signal2<GuildTeacher> sig2_guildTeacher;
    Signal2<GuildPractised> sig2_guildPractised;
    Signal2<CharSkills> sig2_charSkills;
    Signal2<InnOffer> sig2_innOffer;
    Signal2<CharTrophies> sig2_charTrophies;
    /// Every pager line MUME sends, whoever's reply it cuts. The parser treats it as no prompt:
    /// replies stay open, the last prompt is kept, and the next line sent is its answer.
    Signal2<PagerLine> sig2_pager;
    /// A real prompt (a GA chunk that is not the pager), emitted after every reader of that
    /// prompt has published what it completed.
    Signal2<> sig2_realPrompt;
    /// A text MUME showed through its viewer, published instead of MMapper's own window while a
    /// frontend has claimed the viewer. See setViewerClaimed().
    Signal2<ViewText> sig2_viewText;
    Signal2<bool> sig2_toggledEchoMode;
    /// Whether a character is in the game changed: a rent, camp rent or quit line, MUME's
    /// account menu or login prompt, or GMCP Char.Name or Room.Info. See GameStateLines. Not
    /// emitted on connecting or disconnecting, which set it back to UNKNOWN on their own.
    Signal2<GameStateEnum> sig2_gameStateChanged;

    Signal2<MumeTimeEnum> sig2_timeOfDayChanged;
    Signal2<MumeMoonPhaseEnum> sig2_moonPhaseChanged;
    Signal2<MumeMoonVisibilityEnum> sig2_moonVisibilityChanged;
    Signal2<MumeSeasonEnum> sig2_seasonChanged;
    Signal2<PromptWeatherEnum> sig2_weatherChanged;
    Signal2<PromptFogEnum> sig2_fogChanged;
    Signal2<MumeMoment> sig2_tick;

    Signal2<> sig2_gainedLevel;

private:
    MumeTimeEnum m_timeOfDay = MumeTimeEnum::UNKNOWN;
    MumeMoonPhaseEnum m_moonPhase = MumeMoonPhaseEnum::UNKNOWN;
    MumeMoonVisibilityEnum m_moonVisibility = MumeMoonVisibilityEnum::UNKNOWN;
    MumeSeasonEnum m_season = MumeSeasonEnum::UNKNOWN;
    PromptWeatherEnum m_weather = PromptWeatherEnum::NICE;
    PromptFogEnum m_fog = PromptFogEnum::NO_FOG;
    GameStateEnum m_gameState = GameStateEnum::UNKNOWN;
    bool m_viewerClaimed = false;

public:
    void observeConnected();
    void observeDisconnected();
    void observeSentToMud(const QString &ba);
    void observeSentToUser(const QString &ba);
    void observeSentToUserTerminal(SendToUserSourceEnum source, const QString &text, bool goAhead);
    void observeSentToUserGmcp(const GmcpMessage &m);
    void observeSentToUserXml(const XmlElement &element);
    void observeSentToUserCombat(const CombatEvent &event);
    void observeWeatherLine(const WeatherLine &line);
    void observeGround(const GroundState &state);
    void observeRoomContents(const RoomContentsSnapshot &contents);
    void observeContainerEvent(const ContainerEvent &event);
    void observeItemBlock(const ItemBlock &block);
    void observeItemEvent(const ItemEvent &event);
    void observeCharStat(const CharStat &stat) { sig2_charStat.invoke(stat); }
    void observeCharScore(const CharScore &score) { sig2_charScore.invoke(score); }
    void observeCharBurden(const CharBurden &burden) { sig2_charBurden.invoke(burden); }
    void observeCharWimpy(const CharWimpy &wimpy) { sig2_charWimpy.invoke(wimpy); }
    void observeCharFollowers(const CharFollowers &followers)
    {
        sig2_charFollowers.invoke(followers);
    }
    void observeCharLevel(const CharLevel &level) { sig2_charLevel.invoke(level); }
    void observeAccountMenu(const AccountMenu &menu) { sig2_accountMenu.invoke(menu); }
    void observeAccountChars(const AccountChars &chars) { sig2_accountChars.invoke(chars); }
    void observeAccountReply(const AccountReply &reply) { sig2_accountReply.invoke(reply); }
    void observeShopList(const ShopList &list) { sig2_shopList.invoke(list); }
    void observeShopDeal(const ShopDeal &deal) { sig2_shopDeal.invoke(deal); }
    void observeGuildTeacher(const GuildTeacher &teacher) { sig2_guildTeacher.invoke(teacher); }
    void observeGuildPractised(const GuildPractised &practised)
    {
        sig2_guildPractised.invoke(practised);
    }
    void observeCharSkills(const CharSkills &skills) { sig2_charSkills.invoke(skills); }
    void observeInnOffer(const InnOffer &offer) { sig2_innOffer.invoke(offer); }
    void observeCharTrophies(const CharTrophies &trophies) { sig2_charTrophies.invoke(trophies); }
    void observePager(const PagerLine &pager) { sig2_pager.invoke(pager); }
    void observeRealPrompt() { sig2_realPrompt.invoke(); }
    void observeViewText(const ViewText &view) { sig2_viewText.invoke(view); }
    /// Whether a frontend has claimed MUME's viewer: viewed texts then go out as sig2_viewText
    /// and MMapper opens no window of its own for them.
    void setViewerClaimed(const bool claimed) { m_viewerClaimed = claimed; }
    void observeToggledEchoMode(bool echo);
    /// What a line or a GMCP message said about the game state; see nextGameState().
    void observeGameState(GameStateEnum seen);

    void observeTimeOfDay(MumeTimeEnum timeOfDay);
    void observeMoonPhase(MumeMoonPhaseEnum moonPhase);
    void observeMoonVisibility(MumeMoonVisibilityEnum moonVisibility);
    void observeSeason(MumeSeasonEnum season);
    void observeWeather(PromptWeatherEnum weather);
    void observeFog(PromptFogEnum fog);
    void observeTick(const MumeMoment &moment) { sig2_tick.invoke(moment); }

    void observeGainedLevel() { sig2_gainedLevel.invoke(); }

public:
    NODISCARD MumeTimeEnum getTimeOfDay() const { return m_timeOfDay; }
    NODISCARD MumeMoonPhaseEnum getMoonPhase() const { return m_moonPhase; }
    NODISCARD MumeMoonVisibilityEnum getMoonVisibility() const { return m_moonVisibility; }
    NODISCARD MumeSeasonEnum getSeason() const { return m_season; }
    NODISCARD PromptWeatherEnum getWeather() const { return m_weather; }
    NODISCARD PromptFogEnum getFog() const { return m_fog; }
    NODISCARD GameStateEnum getGameState() const { return m_gameState; }
    NODISCARD bool isViewerClaimed() const { return m_viewerClaimed; }
};

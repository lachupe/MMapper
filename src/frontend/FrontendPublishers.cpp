// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

// How each of MMapper's packages reaches the frontends: one registration per package, binding
// the GameObserver signal that carries what the readers found to the builder that makes the
// message. What a late frontend is told of the package is its row in FrontendReplayPolicy.cpp,
// applied by publish(), so a package that is plain state or a plain event is one line here and
// one row there. The packages that need more than that are below the table, each with its reason.

#include "../observer/gameobserver.h"
#include "FrontendMessages.h"
#include "FrontendServer.h"
#include "TradeMessages.h"

#include <QDateTime>

template<typename Event>
void FrontendServer::bindPublisher(Signal2<Event> &signal, GmcpMessage (*const build)(const Event &))
{
    signal.connect(m_lifetime, [this, build](const Event &event) { publish(build(event)); });
}

void FrontendServer::registerPublishers()
{
    using namespace frontend_messages;
    GameObserver &o = m_observer;

    // Events: something that happened, or MUME's one reply to one command. Not replayed.
    bindPublisher(o.sig2_sentToUserXml, makeXmlElement);
    bindPublisher(o.sig2_weatherLine, makeWeatherEvent);
    bindPublisher(o.sig2_containerEvent, makeContainerEvent);
    bindPublisher(o.sig2_accountReply, makeAccountReply);
    bindPublisher(o.sig2_charImproved, makeCharImproved);
    bindPublisher(o.sig2_charConsider, makeCharConsider);
    bindPublisher(o.sig2_charRefused, makeCharRefused);
    bindPublisher(o.sig2_handRaised, makeRoomHandRaised);
    bindPublisher(o.sig2_exitLook, makeRoomLook);
    bindPublisher(o.sig2_shopList, makeShopList);
    bindPublisher(o.sig2_shopDeal, makeShopDeal);
    bindPublisher(o.sig2_guildTeacher, makeGuildTeacher);
    bindPublisher(o.sig2_guildPractised, makeGuildPractised);
    bindPublisher(o.sig2_innOffer, makeInnOffer);
    bindPublisher(o.sig2_charTrophies, makeCharTrophies);
    // A text MUME showed through its viewer, which the driving frontend claimed.
    bindPublisher(o.sig2_viewText, makeViewText);

    // State: kept by publish() as the package's row says (whole, merged fields).
    bindPublisher(o.sig2_charBurden, makeCharBurden);
    bindPublisher(o.sig2_charWimpy, makeCharWimpy);
    bindPublisher(o.sig2_charLanguages, makeCharLanguages);
    bindPublisher(o.sig2_roomDoors, makeRoomDoor);
    bindPublisher(o.sig2_charLevel, makeCharLevel);
    bindPublisher(o.sig2_charAbilities, makeCharAbilities);
    bindPublisher(o.sig2_charSkills, makeCharSkills);

    // The packages below do more than build and publish.

    // A combat event, and what it changed of the effects on the player's character, which is
    // state: sent whole after the event that changed it.
    o.sig2_sentToUserCombat.connect(m_lifetime, [this](const CombatEvent &event) {
        publish(makeCombatEvent(event));
        if (m_charAffects.receiveEvent(event, QDateTime::currentSecsSinceEpoch())) {
            publishCharAffects();
        }
    });

    // The character's figures as the last reply to `stat`, `score` or `info` gave them (Score
    // merged, since `score` restates only the pools), and the effects and wounds they list.
    o.sig2_charStat.connect(m_lifetime, [this](const CharStat &stat) {
        publish(makeCharStat(stat));
        // `stat`'s list sets the tracked effects right: after the reply it came in.
        // Both, then one message: the wounds come in the same reply.
        const bool affectsChanged = m_charAffects.receiveStat(stat.affects);
        const bool woundsChanged = m_charAffects.receiveWounds(stat.wounds);
        if (affectsChanged || woundsChanged) {
            publishCharAffects();
        }
    });
    o.sig2_charScore.connect(m_lifetime, [this](const CharScore &score) {
        publish(makeCharScore(score));
        // `info`'s effects list states the wounds as `stat`'s does.
        if (score.effectsKnown && m_charAffects.receiveWounds(score.wounds)) {
            publishCharAffects();
        }
    });

    // The followers, whole at each change. What a frontend that connects later is told is what
    // lasts of it, which is not what is published.
    o.sig2_charFollowers.connect(m_lifetime, [this](const CharFollowers &followers) {
        m_replay.remember(makeCharFollowers(lastingFollowers(followers)));
        broadcast(makeCharFollowers(followers));
    });

    // MUME's account menu and the account's characters: the login was accepted.
    o.sig2_accountMenu.connect(m_lifetime, [this](const AccountMenu &menu) {
        rememberAccepted();
        publish(makeAccountMenu(menu));
    });
    o.sig2_accountChars.connect(m_lifetime, [this](const AccountChars &chars) {
        rememberAccepted();
        publish(makeAccountChars(chars));
    });

    // Snapshots built from the map and the item readers (see FrontendServer.h).
    o.sig2_groundChanged.connect(m_lifetime,
                                 [this](const GroundState &ground) { onGroundChanged(ground); });
    o.sig2_roomContents.connect(m_lifetime, [this](const RoomContentsSnapshot &contents) {
        onRoomContents(contents);
    });
    o.sig2_itemBlock.connect(m_lifetime, [this](const ItemBlock &block) { onItemBlock(block); });

    // An item moved: no stable object IDs exist in text replies, so the snapshots of what was
    // carried and what lay in the room must not be replayed.
    o.sig2_itemEvent.connect(m_lifetime, [this](const ItemEvent &event) {
        if (event.action != ItemActionEnum::REFUSED) {
            m_replay.clear(ReplayScopeEnum::ITEMS);
        }
        publish(makeCharItem(event));
    });
    o.sig2_itemCommand.connect(m_lifetime, [this](const ItemCommandObservation &command) {
        if (command.status == QStringLiteral("pending")
            && command.action != QStringLiteral("equipment")
            && command.action != QStringLiteral("inventory")
            && command.action != QStringLiteral("look")
            && command.action != QStringLiteral("examine")) {
            m_replay.clear(ReplayScopeEnum::ITEMS);
        }
        publish(makeCharCommand(command));
    });

    // Every second, from MumeClock::slot_tick. Most ticks change nothing a frontend's own clock
    // does not already know; onClockTick() decides.
    o.sig2_tick.connect(m_lifetime, [this](const MumeMoment &moment) {
        onClockTick(moment, QDateTime::currentSecsSinceEpoch());
    });

    // What MUME's own GMCP sent, relayed verbatim under its own name; and the Log's situation:
    // race, hit points, group, who is in the room.
    o.sig2_sentToUserGmcp.connect(m_lifetime, [this](const GmcpMessage &msg) {
        m_importantLog.receiveGmcp(msg);
        // Core and MUME.Client are not relayed: the proxy only filters out the MUME.Client
        // messages it knows, and MUME's Core ones come through, so both are dropped here before
        // anything a frontend subscribed to by name could match them.
        if (!FrontendSubscriptions::isRelayable(msg)) {
            return;
        }
        publish(msg);
    });

    // Terminal output goes to each frontend as its filter has it.
    o.sig2_sentToUserTerminal.connect(m_lifetime,
                                      [this](const TerminalOutput &out) { publishTerminal(out); });
    o.sig2_terminalHidden.connect(m_lifetime, [this](const TerminalHidden &hidden) {
        // An event. A room's description goes only to the frontends that were spared it.
        const GmcpMessage msg = makeTerminalHidden(hidden);
        for (Client &client : m_clients) {
            if (wantsTerminalHidden(hidden, client.filter)) {
                sendTo(client, msg);
            }
        }
    });
}

void FrontendServer::registerSessionHandlers()
{
    GameObserver &o = m_observer;

    o.sig2_connected.connect(m_lifetime, [this]() {
        m_upstreamConnected = true;
        m_loginPrompt = LoginPrompt{};
        // A login held for remembering was the last connection's.
        m_loginMemory.reset();
        // A new game session invalidates everything we cached from the previous one.
        m_replay.clear(ReplayScopeEnum::ACCOUNT);
        m_importantLog.reset();
        m_charAffects.reset();
        publishSessionState();
    });

    o.sig2_disconnected.connect(m_lifetime, [this]() {
        m_upstreamConnected = false;
        m_loginPrompt = LoginPrompt{};
        m_loginMemory.reset();
        // What was carried and what lay in the room is not known any more; the rest of what
        // MUME said stays until a character leaves the game or a new connection begins.
        m_replay.clear(ReplayScopeEnum::ITEMS);
        publishSessionState();
    });

    o.sig2_gameStateChanged.connect(m_lifetime, [this](const GameStateEnum state) {
        if (state != GameStateEnum::PLAYING) {
            // Rented, quit or at the menu: what MUME said of the character, the room and what
            // was carried is not the state of anything now, and must not be replayed to a
            // frontend that connects while MUME waits at its menu. The account's menu and
            // characters stay: they are what MUME's menu shows.
            m_replay.clear(ReplayScopeEnum::GAME);
            // The effects were this character's: the next one to enter starts with none known.
            m_charAffects.reset();
        } else {
            // A character is in the game: no login prompt stands, and the login was accepted.
            m_loginPrompt = LoginPrompt{};
            rememberAccepted();
        }
        publishSessionState();
    });

    o.sig2_toggledEchoMode.connect(m_lifetime, [this](const bool echo) {
        m_echo = echo;
        publishSessionState();
    });

    // State: the login prompt MUME waits at, in MMapper.Session.State's `login`, so that it
    // comes with `game` and `echo` in one message and is replayed with them. Sent at every
    // prompt, the same one again too: that is how a refusal is told.
    o.sig2_loginPrompt.connect(m_lifetime, [this](const LoginPrompt &prompt) {
        m_loginPrompt = prompt;
        publishSessionState();
    });
}

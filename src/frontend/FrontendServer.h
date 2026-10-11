#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../clock/mumemoment.h"
#include "../global/Signal2.h"
#include "../global/macros.h"
#include "../map/roomid.h"
#include "../parser/CharAffects.h"
#include "../parser/LoginLines.h"
#include "../proxy/GmcpMessage.h"
#include "FrontendLoginMemory.h"
#include "FrontendMapIdentity.h"
#include "FrontendMessages.h"
#include "FrontendReplayCache.h"
#include "FrontendSession.h"
#include "FrontendSubscriptions.h"
#include "ImportantLog.h"
#include "TradeOperations.h"

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QObject>
#include <QString>
#include <QTimer>

class ConnectionListener;
class GameObserver;
class MapData;
struct GroundState;
struct ItemBlock;
class MumeClock;
class PasswordConfig;
struct RoomContentsSnapshot;
class QWebSocket;
class QWebSocketServer;

/// A WebSocket endpoint that publishes MMapper's session to external frontends.
///
/// Any number of frontends may watch a session. At most one may drive it, because MMapper
/// accepts a single downstream client: the first frontend to connect while that slot is
/// free takes it and may send input, and every other frontend observes. A frontend that
/// connects while a telnet or built-in client already owns the session observes too. Each
/// frontend is told which it is, in MMapper.Session.State's `role`.
///
/// Frontends speak the same GMCP dialect a telnet client would, minus telnet framing: each
/// WebSocket text frame is one `Package.Name <optional json>` message. A frontend announces
/// what it wants with Core.Supports.Set and receives only those modules, exactly as
/// UserTelnet already does for telnet clients.
///
/// Everything MUME sends is relayed verbatim under its own package name, except Core and
/// MUME.Client, which are never relayed (FrontendSubscriptions::isRelayable). MMapper's own
/// additions live under MMapper.Combat, MMapper.Char, MMapper.Map, MMapper.Room, MMapper.Session,
/// MMapper.Terminal, MMapper.Time, MMapper.Weather, MMapper.Xml and MMapper.Log (the prioritised
/// Log, ImportantLog: MMapper.Log.SetRule and .DeleteRule from the driving frontend only, Explain
/// from any). The one package a frontend
/// sends besides Core.Hello and Core.Supports are MMapper.Input.Command, MMapper.Input.Quiet,
/// MMapper.Session.RememberLogin and MMapper.Trade.Request and .Cancel, and only the driving
/// frontend may send them. RememberLogin {"remember": bool} asks MMapper to keep the login it
/// relays as its own auto-login (LoginMemory; the pass phrase goes only to the keychain). A trade
/// request is run by TradeOperations, which holds the conversation with MUME and publishes
/// MMapper.Trade.Operation; so is a quiet command, one line whose reply goes to the frontend as
/// MMapper.Input.Reply and not to the terminal. MUME's viewer is claimed while the driving
/// frontend subscribes to MMapper.View.
///
/// Any frontend, driving or not, may send MMapper.Terminal.Filter to be spared parts of its own
/// MMapper.Terminal.Output (a room's description): kept per connection, and of no effect on any
/// other frontend, on MMapper's own client or on a telnet client. What was kept from a terminal
/// is told in MMapper.Terminal.Hidden.
///
/// Subscribing brings a frontend up to date: its MMapper.Session.State, then the state MUME
/// has described so far (see FrontendReplayCache), the game clock and the mapped position.
/// Events -- terminal output, XML elements, combat events -- are not replayed. The session
/// state is sent again whenever it changes, the loaded map's name, size and generation
/// included, though the map is announced at most once a second.
///
/// A request that cannot be acted on is answered with MMapper.Session.Error rather than by
/// closing the connection, so that a misbehaving frontend can be debugged. Its `code` is one
/// of "malformed" (the frame names no package), "invalid-supports" (a Core.Supports payload
/// that is missing or is not an array; entries in the array that are not strings are skipped
/// instead), "unsupported" (a package frontends may not send),
/// "read-only" (input from a frontend that is observing), "invalid-command" (an
/// MMapper.Input.Command without a `text` string), "invalid-trade" (an MMapper.Trade.Request
/// or .Cancel without an `id` string), "invalid-quiet" (an MMapper.Input.Quiet without one) and
/// "invalid-filter" (an MMapper.Terminal.Filter that is no object of booleans), "invalid-remember"
/// (an MMapper.Session.RememberLogin without a boolean `remember`) and "remember-unavailable"
/// (remembering asked for in a build without a keychain).
/// Like every package it only reaches a
/// frontend subscribed to its module, MMapper.Session. Only an oversized frame closes the
/// connection.
class NODISCARD_QOBJECT FrontendServer final : public QObject
{
    Q_OBJECT

private:
    struct NODISCARD Client final
    {
        QWebSocket *socket = nullptr;
        FrontendSubscriptions subscriptions;
        QString name;
        /// What this frontend asked to be spared in its terminal output: nothing, to begin with.
        frontend_messages::TerminalFilter filter;
    };

private:
    GameObserver &m_observer;
    MapData &m_mapData;
    ConnectionListener &m_listener;
    QWebSocketServer *m_server = nullptr;
    std::vector<Client> m_clients;

    /// Attached only while a frontend is driving the session.
    FrontendSession m_session;
    /// The frontend holding m_session, or null when none is driving.
    QWebSocket *m_driver = nullptr;

    /// Runs MMapper.Trade.Request through m_session, and keeps MUME's viewer setting.
    TradeOperations m_trade;
    /// The prioritised Log (MMapper.Log): ranks MUME's lines and keeps the user's rules.
    ImportantLog m_importantLog;
    /// Four times a second while an operation runs: its step's time may be up, or MUME idle
    /// at last for a quiet command that waits.
    QTimer m_tradeTimer;

    /// What MUME has said about the current state, with every change since folded in,
    /// replayed to a frontend that connects mid-session so that it does not have to wait
    /// for the next change to become usable.
    FrontendReplayCache m_replayCache;
    /// The effects on the player's character, published whole as MMapper.Char.Affects at
    /// each change and replayed through the cache; forgotten with it.
    CharAffectsTracker m_charAffects;
    /// Fires when the bleeding kept in m_charAffects would end for lack of bleed lines.
    QTimer m_bleedTimer;

    bool m_upstreamConnected = false;
    bool m_echo = true;
    /// The login prompt MUME waits at, MMapper.Session.State's `login`; NONE for none.
    LoginPrompt m_loginPrompt;
    /// MMapper.Session.RememberLogin: the login relayed while it was asked for, held until MUME
    /// accepts it and then handed to the keychain.
    LoginMemory m_loginMemory;
    /// The keychain MMapper's auto-login reads (Proxy's own reads the same entry).
    std::unique_ptr<PasswordConfig> m_passwordConfig;
    /// The account being stored in the keychain, until it says it was.
    QString m_storingAccount;

    /// The loaded map as MMapper.Session.State names it, and when it may next announce a change.
    FrontendMapIdentity m_mapIdentity;
    /// Runs while an announcement of the map is held back (FrontendMapIdentity::announceDelayMs).
    QTimer m_sessionStateTimer;

    /// The game clock, for MMapper.Time.State. Null until setClock(); the moments themselves
    /// arrive on GameObserver::sig2_tick, but only the clock knows how far to trust them.
    MumeClock *m_clock = nullptr;
    /// The last MMapper.Time.State sent, replayed to frontends that subscribe later, and what
    /// it said: frontends run their own clock from it, so it is only sent again when theirs
    /// would now be wrong.
    std::optional<GmcpMessage> m_timeState;
    int64_t m_timeStateMinutes = 0;
    int64_t m_timeStateSentAt = 0;
    int m_timeStateHour = -1;
    int m_timeStatePrecision = -2;
    /// The last MMapper.Weather.Ground sent: the snow, frost and ice where the player stands,
    /// which a frontend that subscribes later needs before the next room display.
    std::optional<GmcpMessage> m_groundState;
    /// The last MMapper.Room.Contents sent: what lies in the room where the player stands,
    /// which a frontend that subscribes later needs before the next room display.
    std::optional<GmcpMessage> m_roomContents;
    /// The last MMapper.Char.Equipment and MMapper.Char.Inventory of the player's own, and the
    /// last MMapper.Char.Container of each of the few containers most recently listed, oldest
    /// first: a frontend that subscribes later can show them before the player lists them again.
    std::optional<GmcpMessage> m_charEquipment;
    std::optional<GmcpMessage> m_charInventory;
    /// A list, since GmcpMessage cannot be assigned, which a deque needs to erase from the middle.
    std::list<std::pair<QString, GmcpMessage>> m_charContainers;

    Signal2Lifetime m_lifetime;

public:
    explicit FrontendServer(GameObserver &observer,
                            MapData &mapData,
                            ConnectionListener &listener,
                            QObject *parent);
    ~FrontendServer() final;

public:
    /// Binds to the loopback interface. Returns false and logs on failure.
    ///
    /// Loopback only, deliberately: a frontend sees everything the player sees, so this is
    /// not something to expose to a network without authentication first.
    NODISCARD bool listen(quint16 port);

    NODISCARD bool isListening() const;
    NODISCARD size_t clientCount() const { return m_clients.size(); }

public:
    /// Publishes MMapper.Map.Position. Connect this to Mmapper2PathMachine::sig_playerMoved
    /// so that it follows MMapper's confirmed position rather than typed movement commands.
    void onPlayerMoved(RoomId id);

    /// A map was loaded, or a new empty one started: `mapGeneration` starts again at 0, and
    /// MMapper.Session.State announces the map. MapData says nothing when it loads a map (its
    /// signals are blocked meanwhile), so MainWindow calls this.
    void onMapLoaded();

    /// The map was saved: announces its new name, if the save gave it one.
    void onMapSaved();

    /// The map may have changed: counts the change in `mapGeneration` and announces it, at most
    /// once per FrontendMapIdentity::ANNOUNCE_INTERVAL_MS. Driven by MapData::sig_onDataChanged;
    /// public for a change made while MapData's signals were blocked.
    void onMapChanged();

    /// Publishes MMapper.Weather.Ground for `ground`, naming the current room, and keeps it
    /// for replay. Driven by GameObserver::sig2_groundChanged; public so tests can drive it.
    void onGroundChanged(const GroundState &ground);

    /// Publishes MMapper.Room.Contents for `contents`, naming the current room, and keeps it
    /// for replay. Driven by GameObserver::sig2_roomContents; public so tests can drive it.
    void onRoomContents(const RoomContentsSnapshot &contents);

    /// Publishes the MMapper.Char package for one listing: Equipment, Inventory or Container.
    /// The player's own equipment and inventory, and each container, are kept for replay; a look
    /// at someone else is not. Driven by GameObserver::sig2_itemBlock; public so tests can drive
    /// it.
    void onItemBlock(const ItemBlock &block);
    /// Gives MMapper.Time.State its source of confidence. Without a clock the moments on
    /// GameObserver::sig2_tick are still published, as precision "unset".
    void setClock(MumeClock &clock);

    /// Publishes MMapper.Time.State for `moment` if a frontend's own running clock would now
    /// be wrong: the hour or the precision changed, or the clock was set again. Called for
    /// every tick; public so that it can be driven without a running MumeClock.
    void onClockTick(const MumeMoment &moment, int64_t nowSecs);

signals:
    void sig_log(const QString &mod, const QString &msg);
    /// A frontend connected or disconnected; `count` is how many are attached now. MainWindow
    /// pauses the map canvas while there is one (FrontendRenderPause).
    void sig_clientCountChanged(int count);

private:
    void onNewConnection();
    void onDisconnected(QWebSocket *socket);
    void onTextMessage(QWebSocket *socket, const QString &frame);

private:
    NODISCARD Client *findClient(QWebSocket *socket);
    void publish(const GmcpMessage &msg);
    static void sendTo(Client &client, const GmcpMessage &msg);
    void replayTo(Client &client);
    /// Gives the session to `client` if nothing else owns it. Returns true if it now drives.
    NODISCARD bool tryTakeSession(const Client &client);
    /// Gives the session to the longest-waiting frontend if it is free and none holds it.
    void offerSession();
    void releaseSession();
    NODISCARD GmcpMessage sessionStateFor(const Client &client) const;
    void handleInput(Client &client, const GmcpMessage &msg);
    /// MMapper.Trade.Request and .Cancel.
    void handleTrade(Client &client, const GmcpMessage &msg);
    /// MMapper.Input.Quiet.
    void handleQuiet(Client &client, const GmcpMessage &msg);
    /// MMapper.Terminal.Filter.
    void handleFilter(Client &client, const GmcpMessage &msg);
    /// MMapper.Session.RememberLogin.
    void handleRememberLogin(Client &client, const GmcpMessage &msg);
    /// MMapper.Log.SetRule, .DeleteRule and .Explain.
    void handleLog(Client &client, const GmcpMessage &msg);
    /// MUME accepted the login: what LoginMemory held goes to the keychain, if it was asked for.
    void rememberAccepted();
    /// MMapper.Terminal.Output, to each frontend as its filter has it.
    void publishTerminal(const TerminalOutput &out);
    /// What TradeOperations is told of the session with a request.
    NODISCARD TradeOperations::Context tradeContext() const;
    /// Works out again whether the driving frontend claims MUME's viewer (claimsViewer()).
    void updateViewerClaim();
    void publishSessionState();
    void publishCharAffects();
    /// Publishes MMapper.Session.State once the map's announcement interval allows: on the next
    /// turn of the event loop, or when the interval is over. Changes made meanwhile ride along.
    void scheduleSessionState();
    void log(const QString &msg);
};

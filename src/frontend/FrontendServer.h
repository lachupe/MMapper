#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../clock/mumemoment.h"
#include "../global/Signal2.h"
#include "../global/macros.h"
#include "../map/roomid.h"
#include "../proxy/GmcpMessage.h"
#include "FrontendReplayCache.h"
#include "FrontendSession.h"
#include "FrontendSubscriptions.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>

class ConnectionListener;
class GameObserver;
class MapData;
struct GroundState;
class MumeClock;
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
/// additions live under MMapper.Combat, MMapper.Map, MMapper.Session, MMapper.Terminal,
/// MMapper.Time and MMapper.Xml. The one package a frontend sends besides Core.Hello and
/// Core.Supports is MMapper.Input.Command, and only the driving frontend may send it.
///
/// Subscribing brings a frontend up to date: its MMapper.Session.State, then the state MUME
/// has described so far (see FrontendReplayCache), the game clock and the mapped position.
/// Events -- terminal output, XML elements, combat events -- are not replayed.
///
/// A request that cannot be acted on is answered with MMapper.Session.Error rather than by
/// closing the connection, so that a misbehaving frontend can be debugged. Its `code` is one
/// of "malformed" (the frame names no package), "invalid-supports" (a Core.Supports payload
/// that is missing or is not an array; entries in the array that are not strings are skipped
/// instead), "unsupported" (a package frontends may not send),
/// "read-only" (input from a frontend that is observing) and "invalid-command" (an
/// MMapper.Input.Command without a `text` string). Like every package it only reaches a
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

    /// What MUME has said about the current state, with every change since folded in,
    /// replayed to a frontend that connects mid-session so that it does not have to wait
    /// for the next change to become usable.
    FrontendReplayCache m_replayCache;

    bool m_upstreamConnected = false;
    bool m_echo = true;

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

    /// Publishes MMapper.Weather.Ground for `ground`, naming the current room, and keeps it
    /// for replay. Driven by GameObserver::sig2_groundChanged; public so tests can drive it.
    void onGroundChanged(const GroundState &ground);

    /// Gives MMapper.Time.State its source of confidence. Without a clock the moments on
    /// GameObserver::sig2_tick are still published, as precision "unset".
    void setClock(MumeClock &clock);

    /// Publishes MMapper.Time.State for `moment` if a frontend's own running clock would now
    /// be wrong: the hour or the precision changed, or the clock was set again. Called for
    /// every tick; public so that it can be driven without a running MumeClock.
    void onClockTick(const MumeMoment &moment, int64_t nowSecs);

signals:
    void sig_log(const QString &mod, const QString &msg);

private:
    void onNewConnection();
    void onDisconnected(QWebSocket *socket);
    void onTextMessage(QWebSocket *socket, const QString &frame);

private:
    NODISCARD Client *findClient(QWebSocket *socket);
    NODISCARD bool isMapLoaded() const;
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
    void publishSessionState();
    void log(const QString &msg);
};

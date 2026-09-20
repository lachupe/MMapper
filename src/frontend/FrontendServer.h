#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/Signal2.h"
#include "../global/macros.h"
#include "../map/roomid.h"
#include "../proxy/GmcpMessage.h"
#include "FrontendSession.h"
#include "FrontendSubscriptions.h"

#include <map>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

class ConnectionListener;
class GameObserver;
class MapData;
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
/// Everything MUME sends is relayed verbatim under its own package name. MMapper's own
/// additions live under MMapper.Map, MMapper.Session and MMapper.Terminal.
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

    /// Last known value of each stateful package, replayed to a frontend that connects
    /// mid-session so that it does not have to wait for the next change to become usable.
    std::map<GmcpMessageTypeEnum, GmcpMessage> m_replayCache;

    bool m_upstreamConnected = false;
    bool m_echo = true;

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
    void rememberIfStateful(const GmcpMessage &msg);
    void publishSessionState();
    void log(const QString &msg);
};

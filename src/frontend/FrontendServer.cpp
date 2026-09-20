// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendServer.h"

#include "../global/TextUtils.h"
#include "../global/utils.h"
#include "../map/RoomHandle.h"
#include "../mapdata/mapdata.h"
#include "../observer/gameobserver.h"
#include "FrontendMessages.h"

#include <algorithm>
#include <optional>

#include <QDebug>
#include <QHostAddress>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {

/// Largest frame we will parse. A frontend only ever sends short control messages, so
/// anything larger is either a bug or an attempt to exhaust memory.
constexpr const qint64 MAX_FRAME_BYTES = 64 * 1024;

/// Packages whose latest value fully describes current state, and which can therefore be
/// replayed to a frontend that connects mid-session.
///
/// Deliberately conservative. Packages that arrive as deltas are only replayable while no
/// delta has been seen since the last full snapshot; see invalidatesSnapshot().
NODISCARD bool isReplayable(const GmcpMessageTypeEnum type)
{
    switch (type) {
    case GmcpMessageTypeEnum::CHAR_NAME:
    case GmcpMessageTypeEnum::CHAR_STATUSVARS:
    case GmcpMessageTypeEnum::CHAR_VITALS:
    case GmcpMessageTypeEnum::EVENT_DARKNESS:
    case GmcpMessageTypeEnum::EVENT_MOON:
    case GmcpMessageTypeEnum::EVENT_SUN:
    case GmcpMessageTypeEnum::GROUP_SET:
    case GmcpMessageTypeEnum::ROOM_CHARS_SET:
    case GmcpMessageTypeEnum::ROOM_INFO:
        return true;
    default:
        return false;
    }
}

/// Maps a delta package onto the snapshot package it invalidates.
///
/// Group.Set and Room.Chars.Set describe a complete set, but Add/Remove/Update mutate it.
/// Once a delta has been seen, the cached snapshot is stale and must not be replayed --
/// a frontend would otherwise be told about characters that have already left the room.
NODISCARD std::optional<GmcpMessageTypeEnum> invalidatesSnapshot(const GmcpMessageTypeEnum type)
{
    switch (type) {
    case GmcpMessageTypeEnum::GROUP_ADD:
    case GmcpMessageTypeEnum::GROUP_REMOVE:
    case GmcpMessageTypeEnum::GROUP_UPDATE:
        return GmcpMessageTypeEnum::GROUP_SET;
    case GmcpMessageTypeEnum::ROOM_CHARS_ADD:
    case GmcpMessageTypeEnum::ROOM_CHARS_REMOVE:
    case GmcpMessageTypeEnum::ROOM_CHARS_UPDATE:
        return GmcpMessageTypeEnum::ROOM_CHARS_SET;
    default:
        return std::nullopt;
    }
}

} // namespace

FrontendServer::FrontendServer(GameObserver &observer, MapData &mapData, QObject *const parent)
    : QObject{parent}
    , m_observer{observer}
    , m_mapData{mapData}
{
    m_observer.sig2_connected.connect(m_lifetime, [this]() {
        m_upstreamConnected = true;
        // A new game session invalidates everything we cached from the previous one.
        m_replayCache.clear();
        publishSessionState();
    });

    m_observer.sig2_disconnected.connect(m_lifetime, [this]() {
        m_upstreamConnected = false;
        publishSessionState();
    });

    m_observer.sig2_toggledEchoMode.connect(m_lifetime, [this](const bool echo) {
        m_echo = echo;
        publishSessionState();
    });

    m_observer.sig2_sentToUserTerminal.connect(m_lifetime, [this](const TerminalOutput &out) {
        publish(frontend_messages::makeTerminalOutput(out.source, out.text, out.goAhead));
    });

    m_observer.sig2_sentToUserGmcp.connect(m_lifetime, [this](const GmcpMessage &msg) {
        // Relayed verbatim: same package name, same payload the game sent. MUME.Client is
        // never seen here; the proxy filters it out before the observer is notified.
        rememberIfStateful(msg);
        publish(msg);
    });
}

FrontendServer::~FrontendServer()
{
    for (Client &client : m_clients) {
        if (client.socket != nullptr) {
            client.socket->close();
        }
    }
    m_clients.clear();
}

bool FrontendServer::listen(const quint16 port)
{
    if (m_server != nullptr) {
        return m_server->isListening();
    }

    m_server = new QWebSocketServer(QStringLiteral("MMapper Frontend"),
                                    QWebSocketServer::NonSecureMode,
                                    this);

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        log(QString("Failed to listen on 127.0.0.1:%1 (%2)")
                .arg(port)
                .arg(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &FrontendServer::onNewConnection);
    log(QString("Listening on ws://127.0.0.1:%1").arg(m_server->serverPort()));
    return true;
}

bool FrontendServer::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

void FrontendServer::onNewConnection()
{
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QWebSocket *const socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }

        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &s) {
            onTextMessage(socket, s);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            onDisconnected(socket);
        });

        m_clients.push_back(Client{socket, FrontendSubscriptions{}, QStringLiteral("unnamed")});
        log(QString("Frontend connected (%1 total)").arg(m_clients.size()));
    }
}

void FrontendServer::onDisconnected(QWebSocket *const socket)
{
    utils::erase_if(m_clients, [socket](const Client &c) { return c.socket == socket; });
    log(QString("Frontend disconnected (%1 remaining)").arg(m_clients.size()));
    socket->deleteLater();
}

FrontendServer::Client *FrontendServer::findClient(QWebSocket *const socket)
{
    const auto it = std::find_if(m_clients.begin(), m_clients.end(), [socket](const Client &c) {
        return c.socket == socket;
    });
    return (it == m_clients.end()) ? nullptr : &*it;
}

void FrontendServer::onTextMessage(QWebSocket *const socket, const QString &frame)
{
    Client *const client = findClient(socket);
    if (client == nullptr) {
        return;
    }

    const QByteArray raw = frame.toUtf8();
    if (raw.size() > MAX_FRAME_BYTES) {
        // Oversized framing is not a normal bad request; drop the connection.
        log(QString("Frontend sent %1 bytes; closing").arg(raw.size()));
        socket->close();
        return;
    }

    const GmcpMessage msg = GmcpMessage::fromRawBytes(raw);
    if (msg.getName().getStdStringUtf8().empty()) {
        sendTo(*client, frontend_messages::makeError("malformed", "Could not parse message"));
        return;
    }

    if (msg.isCoreHello()) {
        if (const auto &optDoc = msg.getJsonDocument()) {
            if (const auto optObj = optDoc->getObject()) {
                client->name = optObj->getString("client").value_or(QStringLiteral("unnamed"));
            }
        }
        log(QString("Frontend identified as '%1'").arg(client->name));
        return;
    }

    if (client->subscriptions.applySupports(msg)) {
        // Subscribing is what makes a frontend usable, so bring it up to date immediately
        // rather than making it wait for the next thing to happen in the game.
        replayTo(*client);
        return;
    }

    if (msg.isCoreSupportsSet() || msg.isCoreSupportsAdd() || msg.isCoreSupportsRemove()) {
        sendTo(*client,
               frontend_messages::makeError("invalid-supports",
                                            "Core.Supports payload must be an array of strings"));
        return;
    }

    // This endpoint is an observer. Input would have to go through MMapper's single
    // downstream session, which a telnet or built-in client already owns.
    sendTo(*client,
           frontend_messages::makeError("read-only",
                                        QString("This endpoint is read-only; '%1' was ignored")
                                            .arg(msg.getName().toQString())));
}

void FrontendServer::rememberIfStateful(const GmcpMessage &msg)
{
    const GmcpMessageTypeEnum type = msg.getType();

    if (const auto stale = invalidatesSnapshot(type)) {
        m_replayCache.erase(*stale);
        return;
    }

    if (isReplayable(type)) {
        // GmcpMessage is copy constructible but not copy assignable, so replace rather
        // than assign.
        m_replayCache.erase(type);
        m_replayCache.emplace(type, msg);
    }
}

bool FrontendServer::isMapLoaded() const
{
    return m_mapData.getCurrentMap().getRoomsCount() != 0;
}

void FrontendServer::publish(const GmcpMessage &msg)
{
    for (Client &client : m_clients) {
        sendTo(client, msg);
    }
}

void FrontendServer::sendTo(Client &client, const GmcpMessage &msg)
{
    if (client.socket == nullptr || !client.subscriptions.wants(msg)) {
        return;
    }
    client.socket->sendTextMessage(QString::fromUtf8(msg.toRawBytes()));
}

void FrontendServer::replayTo(Client &client)
{
    sendTo(client,
           frontend_messages::makeSessionState(m_upstreamConnected, isMapLoaded(), m_echo));

    for (const auto &[type, msg] : m_replayCache) {
        sendTo(client, msg);
    }

    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (const auto room = m_mapData.findRoomHandle(*optId)) {
            sendTo(client, frontend_messages::makeMapPosition(room));
        }
    }
}

void FrontendServer::publishSessionState()
{
    publish(frontend_messages::makeSessionState(m_upstreamConnected, isMapLoaded(), m_echo));
}

void FrontendServer::onPlayerMoved(const RoomId id)
{
    if (const auto room = m_mapData.findRoomHandle(id)) {
        publish(frontend_messages::makeMapPosition(room));
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendServer.h"

#include "../clock/mumeclock.h"
#include "../global/TextUtils.h"
#include "../global/utils.h"
#include "../map/RoomHandle.h"
#include "../mapdata/mapdata.h"
#include "../observer/gameobserver.h"
#include "../proxy/connectionlistener.h"
#include "FrontendMessages.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <tuple>

#include <QDateTime>
#include <QDebug>
#include <QHostAddress>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {

/// Largest frame we will parse. A frontend only ever sends short control messages, so
/// anything larger is either a bug or an attempt to exhaust memory.
constexpr const qint64 MAX_FRAME_BYTES = 64 * 1024;

/// How many containers' MMapper.Char.Container are kept for replay: a backpack, a pouch or two,
/// a keyring, and the corpse or chest last looked into.
constexpr const size_t CONTAINERS_KEPT = 8;

} // namespace

FrontendServer::FrontendServer(GameObserver &observer,
                               MapData &mapData,
                               ConnectionListener &listener,
                               QObject *const parent)
    : QObject{parent}
    , m_observer{observer}
    , m_mapData{mapData}
    , m_listener{listener}
{
    m_observer.sig2_connected.connect(m_lifetime, [this]() {
        m_upstreamConnected = true;
        // A new game session invalidates everything we cached from the previous one.
        m_replayCache.clear();
        m_groundState.reset();
        m_roomContents.reset();
        m_charEquipment.reset();
        m_charInventory.reset();
        m_charContainers.clear();
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

    // Queued on purpose: the signal is emitted while the old session is still being torn
    // down, so the slot only actually frees up once the event loop has unwound.
    connect(&m_listener,
            &ConnectionListener::sig_clientDisconnected,
            this,
            &FrontendServer::offerSession,
            Qt::QueuedConnection);

    m_observer.sig2_sentToUserCombat.connect(m_lifetime, [this](const CombatEvent &event) {
        // An event, like the XML elements: something that happened, not state to replay.
        publish(frontend_messages::makeCombatEvent(event));
    });

    m_observer.sig2_sentToUserXml.connect(m_lifetime, [this](const XmlElement &element) {
        // Not cached for replay: these are events, and an element that closed before a
        // frontend connected describes something that has already happened.
        publish(frontend_messages::makeXmlElement(element));
    });

    m_observer.sig2_weatherLine.connect(m_lifetime, [this](const WeatherLine &line) {
        // An event: a line MUME said, not state to replay. What it says about the ground
        // where the player stands is replayed as MMapper.Weather.Ground instead.
        publish(frontend_messages::makeWeatherEvent(line));
    });

    m_observer.sig2_groundChanged.connect(m_lifetime, [this](const GroundState &ground) {
        onGroundChanged(ground);
    });

    m_observer.sig2_roomContents.connect(m_lifetime, [this](const RoomContentsSnapshot &contents) {
        onRoomContents(contents);
    });

    m_observer.sig2_containerEvent.connect(m_lifetime, [this](const ContainerEvent &event) {
        // An event: one reply to one command. What it says about the container rides along in
        // the next MMapper.Room.Contents, which is replayed.
        publish(frontend_messages::makeContainerEvent(event));
    });

    m_observer.sig2_itemBlock.connect(m_lifetime,
                                      [this](const ItemBlock &block) { onItemBlock(block); });

    m_observer.sig2_itemEvent.connect(m_lifetime, [this](const ItemEvent &event) {
        // An event: one reply that moved one thing. The next listing is the state.
        publish(frontend_messages::makeCharItem(event));
    });

    // Every second, from MumeClock::slot_tick. Most ticks change nothing a frontend's own clock
    // does not already know; onClockTick() decides.
    m_observer.sig2_tick.connect(m_lifetime, [this](const MumeMoment &moment) {
        onClockTick(moment, QDateTime::currentSecsSinceEpoch());
    });

    m_observer.sig2_sentToUserGmcp.connect(m_lifetime, [this](const GmcpMessage &msg) {
        // Relayed verbatim: same package name, same payload the game sent. Core and
        // MUME.Client are not: the proxy only filters out the MUME.Client messages it knows,
        // and MUME's Core ones come through, so both are dropped here before anything a
        // frontend subscribed to by name could match them.
        if (!FrontendSubscriptions::isRelayable(msg)) {
            return;
        }
        m_replayCache.remember(msg);
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
        log(QString("Failed to listen on 127.0.0.1:%1 (%2)").arg(port).arg(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &FrontendServer::onNewConnection);
    log(QString("Listening on ws://127.0.0.1:%1").arg(m_server->serverPort()));
    return true;
}

void FrontendServer::log(const QString &msg)
{
    // Also to the terminal: the frontend is driven by an external program, and its state
    // transitions are the first thing to check when that program misbehaves.
    qInfo() << "[frontend]" << msg;
    emit sig_log("Frontend", msg);
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
        // Offered the session straight away, so that a frontend used on its own is a
        // complete client rather than a viewer waiting for someone else to log in.
        const bool driving = tryTakeSession(m_clients.back());
        log(QString("Frontend connected, %1 (%2 total)")
                .arg(driving ? "driving" : "observing")
                .arg(m_clients.size()));
    }
}

void FrontendServer::onDisconnected(QWebSocket *const socket)
{
    const bool wasDriving = m_driver == socket;
    if (wasDriving) {
        releaseSession();
    }
    utils::erase_if(m_clients, [socket](const Client &c) { return c.socket == socket; });
    log(QString("Frontend disconnected (%1 remaining)").arg(m_clients.size()));
    socket->deleteLater();

    // The freed session is picked up by offerSession(), once the proxy teardown started by
    // releaseSession() has finished and ConnectionListener reports the slot free again.
}

void FrontendServer::offerSession()
{
    if (m_driver != nullptr || m_clients.empty()) {
        return;
    }
    log("Session slot freed; offering it to a waiting frontend");
    if (tryTakeSession(m_clients.front())) {
        publishSessionState();
    }
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
        log(QString("Frontend identified as '%1'%2")
                .arg(client->name, m_driver == socket ? " (driving)" : ""));
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
                                            "Core.Supports payload must be an array"));
        return;
    }

    if (msg.isMMapperInputCommand()) {
        handleInput(*client, msg);
        return;
    }

    sendTo(*client,
           frontend_messages::makeError("unsupported",
                                        QString("'%1' is not accepted by this endpoint")
                                            .arg(msg.getName().toQString())));
}

bool FrontendServer::tryTakeSession(const Client &client)
{
    if (m_driver != nullptr) {
        return m_driver == client.socket;
    }
    if (!m_session.attach(m_listener)) {
        return false; // A telnet or built-in client owns the session.
    }
    m_driver = client.socket;
    log(QString("Frontend '%1' is driving the session").arg(client.name));
    return true;
}

void FrontendServer::releaseSession()
{
    if (m_driver == nullptr) {
        return;
    }
    m_driver = nullptr;
    // Closing the session ends the MUME connection, exactly as closing a telnet client does.
    m_session.detach();
    log("Frontend released the session");
}

GmcpMessage FrontendServer::sessionStateFor(const Client &client) const
{
    return frontend_messages::makeSessionState(m_upstreamConnected,
                                               isMapLoaded(),
                                               m_echo,
                                               m_driver != nullptr && m_driver == client.socket);
}

void FrontendServer::handleInput(Client &client, const GmcpMessage &msg)
{
    if (m_driver != client.socket) {
        sendTo(client,
               frontend_messages::makeError("read-only",
                                            "Another client owns the session; this "
                                            "connection may only observe it"));
        return;
    }
    const auto &optDoc = msg.getJsonDocument();
    const auto optObj = optDoc.has_value() ? optDoc->getObject() : std::nullopt;
    if (!optObj.has_value()) {
        sendTo(client,
               frontend_messages::makeError("invalid-command",
                                            "MMapper.Input.Command needs an object payload"));
        return;
    }
    const auto optText = optObj->getString("text");
    if (!optText.has_value()) {
        sendTo(client,
               frontend_messages::makeError("invalid-command",
                                            "MMapper.Input.Command needs a 'text' string"));
        return;
    }
    // Routed through the normal downstream path, so mapper commands, movement tracking and
    // logging see it exactly as they see input from the built-in client. The text itself is
    // never logged here: it may be a password.
    m_session.sendLine(optText.value());
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
    sendTo(client, sessionStateFor(client));

    for (const auto &[type, msg] : m_replayCache.messages()) {
        sendTo(client, msg);
    }

    if (m_timeState.has_value()) {
        sendTo(client, *m_timeState);
    }

    if (m_groundState.has_value()) {
        sendTo(client, *m_groundState);
    }

    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (const auto room = m_mapData.findRoomHandle(*optId)) {
            sendTo(client, frontend_messages::makeMapPosition(room));
        }
    }

    // After the position, as it is published live: the objects belong to the room it names.
    if (m_roomContents.has_value()) {
        sendTo(client, *m_roomContents);
    }

    if (m_charEquipment.has_value()) {
        sendTo(client, *m_charEquipment);
    }
    if (m_charInventory.has_value()) {
        sendTo(client, *m_charInventory);
    }
    for (const auto &[key, msg] : m_charContainers) {
        sendTo(client, msg);
    }
}

void FrontendServer::publishSessionState()
{
    // Sent per connection rather than broadcast: `role` differs between clients.
    for (Client &client : m_clients) {
        sendTo(client, sessionStateFor(client));
    }
}

void FrontendServer::onPlayerMoved(const RoomId id)
{
    if (const auto room = m_mapData.findRoomHandle(id)) {
        publish(frontend_messages::makeMapPosition(room));
    }
}

void FrontendServer::onGroundChanged(const GroundState &ground)
{
    // GroundTracker reports at the prompt that ends a room display, and the path machine has
    // moved the player by then (MumeXmlParser moves at the prompt before emitting its
    // elements), so the current room is the one this ground was seen in.
    std::optional<RoomHandle> room;
    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (RoomHandle handle = m_mapData.findRoomHandle(*optId)) {
            room.emplace(std::move(handle));
        }
    }
    // GmcpMessage is copy constructible but not copy assignable, so replace rather than assign.
    m_groundState.reset();
    m_groundState.emplace(
        frontend_messages::makeGroundState(ground, room.has_value() ? &*room : nullptr));
    publish(*m_groundState);
}

void FrontendServer::onRoomContents(const RoomContentsSnapshot &contents)
{
    // Reported at the prompt that ends a room display, after the path machine has moved the
    // player (see onGroundChanged), so the current room is the one these objects lie in.
    std::optional<RoomHandle> room;
    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (RoomHandle handle = m_mapData.findRoomHandle(*optId)) {
            room.emplace(std::move(handle));
        }
    }
    // GmcpMessage is copy constructible but not copy assignable, so replace rather than assign.
    m_roomContents.reset();
    m_roomContents.emplace(
        frontend_messages::makeRoomContents(contents, room.has_value() ? &*room : nullptr));
    publish(*m_roomContents);
}

void FrontendServer::onItemBlock(const ItemBlock &block)
{
    // GmcpMessage is copy constructible but not copy assignable, so replace rather than assign.
    switch (block.kind) {
    case ItemBlockKindEnum::EQUIPMENT: {
        GmcpMessage msg = frontend_messages::makeCharEquipment(block);
        if (block.owner == QStringLiteral("you")) {
            m_charEquipment.reset();
            m_charEquipment.emplace(msg);
        }
        publish(msg);
        break;
    }
    case ItemBlockKindEnum::INVENTORY: {
        GmcpMessage msg = frontend_messages::makeCharInventory(block);
        if (!block.peek) {
            m_charInventory.reset();
            m_charInventory.emplace(msg);
        }
        publish(msg);
        break;
    }
    case ItemBlockKindEnum::CONTAINER: {
        GmcpMessage msg = frontend_messages::makeCharContainer(block);
        // One per container: "backpack" worn and "backpack" carried are two.
        const QString key = block.keyword + QLatin1Char('|') + block.where;
        const auto it = std::find_if(m_charContainers.begin(),
                                     m_charContainers.end(),
                                     [&key](const auto &entry) { return entry.first == key; });
        if (it != m_charContainers.end()) {
            m_charContainers.erase(it);
        }
        m_charContainers.emplace_back(key, msg);
        while (m_charContainers.size() > CONTAINERS_KEPT) {
            m_charContainers.pop_front();
        }
        publish(msg);
        break;
    }
    }
}

void FrontendServer::setClock(MumeClock &clock)
{
    m_clock = &clock;
}

void FrontendServer::onClockTick(const MumeMoment &moment, const int64_t nowSecs)
{
    const MumeClockPrecisionEnum precision = (m_clock != nullptr) ? m_clock->getPrecision(nowSecs)
                                                                  : MumeClockPrecisionEnum::UNSET;

    // MUME minutes since its epoch; one passes every real second, so a frontend that was told
    // the time `nowSecs - m_timeStateSentAt` seconds ago believes it is `expected` now.
    const int64_t minutes = moment.toSeconds();
    const int64_t expected = m_timeStateMinutes + (nowSecs - m_timeStateSentAt);
    const bool setAgain = std::llabs(minutes - expected) > 1;
    const bool changed = moment.hour != m_timeStateHour
                         || static_cast<int>(precision) != m_timeStatePrecision;
    if (m_timeState.has_value() && !setAgain && !changed) {
        return;
    }

    // GmcpMessage is copy constructible but not copy assignable, so replace rather than assign.
    m_timeState.reset();
    m_timeState.emplace(frontend_messages::makeTimeState(moment, precision));
    m_timeStateMinutes = minutes;
    m_timeStateSentAt = nowSecs;
    m_timeStateHour = moment.hour;
    m_timeStatePrecision = static_cast<int>(precision);
    publish(*m_timeState);
}

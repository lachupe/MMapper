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
#include "TradeMessages.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <tuple>

#include <QDateTime>
#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {

/// Largest frame we will parse. A frontend only ever sends short control messages, so
/// anything larger is either a bug or an attempt to exhaust memory.
constexpr const qint64 MAX_FRAME_BYTES = 64 * 1024;

/// How many containers' MMapper.Char.Container are kept for replay: a backpack, a pouch or two,
/// a keyring, and the corpse or chest last looked into.
constexpr const size_t CONTAINERS_KEPT = 8;

/// Milliseconds on a clock that only moves forward, for spacing out announcements.
NODISCARD int64_t steadyNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

FrontendServer::FrontendServer(GameObserver &observer,
                               MapData &mapData,
                               ConnectionListener &listener,
                               QObject *const parent)
    : QObject{parent}
    , m_observer{observer}
    , m_mapData{mapData}
    , m_listener{listener}
    , m_trade{observer,
              [this](const QString &line) { m_session.sendLine(line); },
              [this](const GmcpMessage &msg) { publish(msg); }}
{
    // The viewer setting is in MMapper.Session.State.
    m_trade.setViewerChanged([this]() { publishSessionState(); });
    m_tradeTimer.setInterval(1000);
    connect(&m_tradeTimer, &QTimer::timeout, this, [this]() {
        m_trade.expire();
        if (!m_trade.busy()) {
            m_tradeTimer.stop();
        }
    });

    // Whatever map MainWindow has by now counts as loaded; a map loaded later calls onMapLoaded().
    m_mapIdentity.loaded(m_mapData.getCurrentMap(), m_mapData.getFileName());
    m_sessionStateTimer.setSingleShot(true);
    connect(&m_sessionStateTimer, &QTimer::timeout, this, &FrontendServer::publishSessionState);
    connect(&m_mapData, &MapData::sig_onDataChanged, this, &FrontendServer::onMapChanged);

    m_observer.sig2_connected.connect(m_lifetime, [this]() {
        m_upstreamConnected = true;
        // A new game session invalidates everything we cached from the previous one.
        m_replayCache.clear();
        m_charAffects.reset();
        m_groundState.reset();
        m_roomContents.reset();
        m_charEquipment.reset();
        m_charInventory.reset();
        m_charContainers.clear();
        publishSessionState();
    });

    m_observer.sig2_disconnected.connect(m_lifetime, [this]() {
        m_upstreamConnected = false;
        m_charEquipment.reset();
        m_charInventory.reset();
        m_charContainers.clear();
        m_roomContents.reset();
        publishSessionState();
    });

    m_observer.sig2_gameStateChanged.connect(m_lifetime, [this](const GameStateEnum state) {
        if (state != GameStateEnum::PLAYING) {
            // Rented, quit or at the menu: what MUME said of the character, the room and what
            // was carried is not the state of anything now, and must not be replayed to a
            // frontend that connects while MUME waits at its menu. The account's menu and
            // characters stay: they are what MUME's menu shows.
            m_replayCache.clearGame();
            // The effects were this character's: the next one to enter starts with none known.
            m_charAffects.reset();
            m_groundState.reset();
            m_roomContents.reset();
            m_charEquipment.reset();
            m_charInventory.reset();
            m_charContainers.clear();
        }
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
        // What it changed of the effects on the player's character is state, though: kept,
        // and sent whole after the event that changed it.
        if (m_charAffects.receiveEvent(event, QDateTime::currentSecsSinceEpoch())) {
            publishCharAffects();
        }
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
        // No stable object IDs exist in text replies; stale snapshots must not be replayed.
        if (event.action != ItemActionEnum::REFUSED) {
            m_charEquipment.reset();
            m_charInventory.reset();
            m_charContainers.clear();
            m_roomContents.reset();
        }
        // An event: one reply that moved one thing. The next listing is the state.
        publish(frontend_messages::makeCharItem(event));
    });

    // State: MUME's account menu and the account's characters, as last printed; kept while no
    // character plays, which is when a frontend wants them. The one-line answers are events.
    m_observer.sig2_accountMenu.connect(m_lifetime, [this](const AccountMenu &menu) {
        const GmcpMessage msg = frontend_messages::makeAccountMenu(menu);
        m_replayCache.remember(msg);
        publish(msg);
    });
    m_observer.sig2_accountChars.connect(m_lifetime, [this](const AccountChars &chars) {
        const GmcpMessage msg = frontend_messages::makeAccountChars(chars);
        m_replayCache.remember(msg);
        publish(msg);
    });
    m_observer.sig2_accountReply.connect(m_lifetime, [this](const AccountReply &reply) {
        publish(frontend_messages::makeAccountReply(reply));
    });

    // State: the character's figures as the last reply to `stat`, `score` or `info` gave them,
    // replayed through the cache (Score merged, since `score` restates only the pools).
    m_observer.sig2_charStat.connect(m_lifetime, [this](const CharStat &stat) {
        const GmcpMessage msg = frontend_messages::makeCharStat(stat);
        m_replayCache.remember(msg);
        publish(msg);
        // `stat`'s list sets the tracked effects right: after the reply it came in.
        if (m_charAffects.receiveStat(stat.affects)) {
            publishCharAffects();
        }
    });
    m_observer.sig2_charScore.connect(m_lifetime, [this](const CharScore &score) {
        const GmcpMessage msg = frontend_messages::makeCharScore(score);
        m_replayCache.remember(msg);
        publish(msg);
    });
    m_observer.sig2_charBurden.connect(m_lifetime, [this](const CharBurden &burden) {
        const GmcpMessage msg = frontend_messages::makeCharBurden(burden);
        m_replayCache.remember(msg);
        publish(msg);
    });

    // State: the followers of the player's character, whole at each change. What a frontend
    // that connects later is told is what lasts of it: not the answer to an order that is
    // over, nor a follower that left or died, which is in the one message that says so.
    m_observer.sig2_charFollowers.connect(m_lifetime, [this](const CharFollowers &followers) {
        m_replayCache.remember(frontend_messages::makeCharFollowers(lastingFollowers(followers)));
        publish(frontend_messages::makeCharFollowers(followers));
    });

    // State: the wimpy as MUME last stated it.
    m_observer.sig2_charWimpy.connect(m_lifetime, [this](const CharWimpy &wimpy) {
        const GmcpMessage msg = frontend_messages::makeCharWimpy(wimpy);
        m_replayCache.remember(msg);
        publish(msg);
    });

    // An event: one sentence that refused one command.
    m_observer.sig2_charRefused.connect(m_lifetime, [this](const CharRefused &refused) {
        publish(frontend_messages::makeCharRefused(refused));
    });

    // State: the doors of the current room as MUME told the player, whole at each change.
    m_observer.sig2_roomDoors.connect(m_lifetime, [this](const RoomDoors &doors) {
        const GmcpMessage msg = frontend_messages::makeRoomDoor(doors);
        m_replayCache.remember(msg);
        publish(msg);
    });

    // State: the level, experience and travel points, as the last reply to CHAR_LEVEL_REQUEST
    // gave them.
    m_observer.sig2_charLevel.connect(m_lifetime, [this](const CharLevel &level) {
        const GmcpMessage msg = frontend_messages::makeCharLevel(level);
        m_replayCache.remember(msg);
        publish(msg);
    });

    // MUME's replies at shops, guilds and inns, and to `trop`: events, except the general practice
    // table, which is the character's own skills and is replayed as last sent.
    m_observer.sig2_shopList.connect(m_lifetime, [this](const ShopList &list) {
        publish(frontend_messages::makeShopList(list));
    });
    m_observer.sig2_shopDeal.connect(m_lifetime, [this](const ShopDeal &deal) {
        publish(frontend_messages::makeShopDeal(deal));
    });
    m_observer.sig2_guildTeacher.connect(m_lifetime, [this](const GuildTeacher &teacher) {
        publish(frontend_messages::makeGuildTeacher(teacher));
    });
    m_observer.sig2_guildPractised.connect(m_lifetime, [this](const GuildPractised &practised) {
        publish(frontend_messages::makeGuildPractised(practised));
    });
    m_observer.sig2_charSkills.connect(m_lifetime, [this](const CharSkills &skills) {
        const GmcpMessage msg = frontend_messages::makeCharSkills(skills);
        m_replayCache.remember(msg);
        publish(msg);
    });
    m_observer.sig2_innOffer.connect(m_lifetime, [this](const InnOffer &offer) {
        publish(frontend_messages::makeInnOffer(offer));
    });
    m_observer.sig2_charTrophies.connect(m_lifetime, [this](const CharTrophies &trophies) {
        publish(frontend_messages::makeCharTrophies(trophies));
    });
    // A text MUME showed through its viewer, which the driving frontend claimed (the proxy then
    // opens no window of its own). An event, not replayed.
    m_observer.sig2_viewText.connect(m_lifetime, [this](const ViewText &view) {
        publish(frontend_messages::makeViewText(view));
    });

    m_observer.sig2_itemCommand.connect(m_lifetime, [this](const ItemCommandObservation &command) {
        if (command.status == QStringLiteral("pending")
            && command.action != QStringLiteral("equipment")
            && command.action != QStringLiteral("inventory")
            && command.action != QStringLiteral("look")
            && command.action != QStringLiteral("examine")) {
            m_charEquipment.reset();
            m_charInventory.reset();
            m_charContainers.clear();
            m_roomContents.reset();
        }
        publish(frontend_messages::makeCharCommand(command));
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
        emit sig_clientCountChanged(static_cast<int>(m_clients.size()));
    }
}

void FrontendServer::onDisconnected(QWebSocket *const socket)
{
    const bool wasDriving = m_driver == socket;
    if (wasDriving) {
        releaseSession();
    }
    utils::erase_if(m_clients, [socket](const Client &c) { return c.socket == socket; });
    updateViewerClaim();
    log(QString("Frontend disconnected (%1 remaining)").arg(m_clients.size()));
    emit sig_clientCountChanged(static_cast<int>(m_clients.size()));
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
        updateViewerClaim();
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

    if (msg.isMMapperTradeRequest() || msg.isMMapperTradeCancel()) {
        handleTrade(*client, msg);
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
    m_trade.setDriving(true);
    updateViewerClaim();
    log(QString("Frontend '%1' is driving the session").arg(client.name));
    return true;
}

void FrontendServer::releaseSession()
{
    if (m_driver == nullptr) {
        return;
    }
    m_driver = nullptr;
    // Nothing more can be sent for an operation; it stops where it is.
    m_trade.setDriving(false);
    updateViewerClaim();
    // Closing the session ends the MUME connection, exactly as closing a telnet client does.
    m_session.detach();
    log("Frontend released the session");
}

GmcpMessage FrontendServer::sessionStateFor(const Client &client) const
{
    return frontend_messages::makeSessionState(m_upstreamConnected,
                                               m_mapIdentity.get(),
                                               m_echo,
                                               m_driver != nullptr && m_driver == client.socket,
                                               m_observer.getGameState(),
                                               m_trade.viewerState());
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
    //
    // The player's line always goes at once; a trade operation holding MUME's pager answers it
    // with `q` first, and stops.
    m_trade.beforePlayerLine(optText.value());
    m_session.sendLine(optText.value());
}

void FrontendServer::handleTrade(Client &client, const GmcpMessage &msg)
{
    const auto &optJson = msg.getJson();
    const QJsonDocument doc = optJson.has_value() ? QJsonDocument::fromJson(optJson->toQByteArray())
                                                  : QJsonDocument{};
    const QJsonObject payload = doc.object();
    const QJsonValue idValue = payload.value("id");
    const QString id = idValue.toString();
    if (!doc.isObject() || !idValue.isString() || id.isEmpty()
        || id.size() > TradeOperations::MAX_ID_CHARS) {
        sendTo(client,
               frontend_messages::makeError("invalid-trade",
                                            QString("'%1' needs an object payload with an 'id' "
                                                    "string")
                                                .arg(msg.getName().toQString())));
        return;
    }

    const bool driving = m_driver != nullptr && m_driver == client.socket;
    if (!driving) {
        // Told to the observer alone: the operations everyone sees are the driving frontend's.
        if (msg.isMMapperTradeRequest()) {
            TradeOperationState refused;
            refused.id = id;
            refused.action = payload.value("action").toString();
            refused.status = TradeStatusEnum::REFUSED;
            refused.reason = QStringLiteral("observing");
            sendTo(client, makeTradeOperation(refused));
        } else {
            sendTo(client,
                   frontend_messages::makeError("read-only",
                                                "Another client owns the session; this "
                                                "connection may only observe it"));
        }
        return;
    }

    if (msg.isMMapperTradeCancel()) {
        m_trade.cancel(id);
        return;
    }

    TradeOperations::Context context;
    context.driving = true;
    context.connected = m_upstreamConnected;
    context.echo = m_echo;
    context.game = m_observer.getGameState();
    m_trade.request(payload, context);
    if (m_trade.busy() && !m_tradeTimer.isActive()) {
        m_tradeTimer.start();
    }
}

void FrontendServer::updateViewerClaim()
{
    const bool claimed = std::any_of(m_clients.begin(), m_clients.end(), [this](const Client &c) {
        return claimsViewer(c.subscriptions, m_driver != nullptr && c.socket == m_driver);
    });
    if (claimed != m_observer.isViewerClaimed()) {
        log(claimed ? "The driving frontend claims MUME's viewer"
                    : "MUME's viewer is no longer claimed");
    }
    m_observer.setViewerClaimed(claimed);
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

void FrontendServer::publishCharAffects()
{
    const GmcpMessage msg = frontend_messages::makeCharAffects(m_charAffects.affects());
    m_replayCache.remember(msg);
    publish(msg);
}

void FrontendServer::publishSessionState()
{
    // Whatever was waiting to be announced goes out now, in this state.
    m_sessionStateTimer.stop();
    m_mapIdentity.announced(steadyNowMs());

    // Sent per connection rather than broadcast: `role` differs between clients.
    for (Client &client : m_clients) {
        sendTo(client, sessionStateFor(client));
    }
}

void FrontendServer::scheduleSessionState()
{
    if (m_sessionStateTimer.isActive()) {
        return; // Already due, and it will say this too.
    }
    // Even with no wait it goes out on the next turn of the event loop, not now, so that what
    // happens in one go -- a new map clearing the old one, a batch of changes -- is one message.
    const int64_t delay = m_mapIdentity.announceDelayMs(steadyNowMs());
    m_sessionStateTimer.start(
        static_cast<int>(std::min<int64_t>(delay, FrontendMapIdentity::ANNOUNCE_INTERVAL_MS)));
}

void FrontendServer::onMapLoaded()
{
    m_mapIdentity.loaded(m_mapData.getCurrentMap(), m_mapData.getFileName());
    scheduleSessionState();
}

void FrontendServer::onMapSaved()
{
    if (m_mapIdentity.renamed(m_mapData.getFileName())) {
        scheduleSessionState();
    }
}

void FrontendServer::onMapChanged()
{
    if (m_mapIdentity.changed(m_mapData.getCurrentMap())) {
        scheduleSessionState();
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

void FrontendServer::onRoomContents(const RoomContentsSnapshot &observed)
{
    RoomContentsSnapshot contents = observed;
    contents.snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (contents.entered) {
        m_charContainers.clear();
    }
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

void FrontendServer::onItemBlock(const ItemBlock &observed)
{
    ItemBlock block = observed;
    block.snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        // Without an isolated query selector the header cannot distinguish identical pouches.
        if (block.target.isEmpty()) {
            m_charContainers.clear();
            publish(msg);
            break;
        }
        // A closed reply has no location: it must replace the previous open observation
        // for this selector rather than leave both in the replay cache.
        const QString key = block.target;
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

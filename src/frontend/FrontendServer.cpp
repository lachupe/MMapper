// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendServer.h"

#include "../clock/mumeclock.h"
#include "../configuration/PasswordConfig.h"
#include "../configuration/configuration.h"
#include "../global/ConfigConsts.h"
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
#include <QDir>
#include <QFileInfo>
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
    , m_importantLog{[this](const GmcpMessage &msg) { publish(msg); }}
    , m_loginMemory{!NO_QTKEYCHAIN}
    , m_passwordConfig{std::make_unique<PasswordConfig>()}
{
    // The keychain's answers to a login stored for MMapper.Session.RememberLogin. Its errors are
    // its own words, never the pass phrase; neither is the pass phrase logged anywhere here.
    connect(m_passwordConfig.get(), &PasswordConfig::sig_passwordSaved, this, [this]() {
        if (m_storingAccount.isEmpty()) {
            return;
        }
        auto &account = setConfig().account;
        account.accountName = m_storingAccount;
        account.accountPassword = true;
        account.rememberLogin = true;
        getConfig().write();
        log(QString("Remembered the login of account '%1' in the keychain").arg(m_storingAccount));
        m_storingAccount.clear();
        m_loginMemory.clearUnavailable();
        publishSessionState();
    });
    connect(m_passwordConfig.get(), &PasswordConfig::sig_error, this, [this](const QString &error) {
        if (m_storingAccount.isEmpty()) {
            return;
        }
        m_storingAccount.clear();
        log(QString("Could not remember the login: %1").arg(error));
        m_loginMemory.setUnavailable(error);
        publishSessionState();
    });

    // The prioritised Log: the shipped rules and the user's file beside MMapper's settings.
    m_importantLog.setOutdoors([this]() {
        // The game's own flag, as mapped: outdoors unless the room is no-sundeath.
        if (const auto optId = m_mapData.getCurrentRoomId()) {
            if (const auto room = m_mapData.findRoomHandle(*optId)) {
                return room.getSundeathType() != RoomSundeathEnum::NO_SUNDEATH;
            }
        }
        return true;
    });
    m_importantLog.loadDefaults();
    m_importantLog.loadUserFile(QFileInfo{getSettingsFileName()}.absoluteDir().filePath(
        QStringLiteral("MMapper-log-rules.json")));
    log(QString("Log rules: %1 defaults, user file %2")
            .arg(m_importantLog.rules().defaultCount())
            .arg(m_importantLog.rules().filePath()));
    m_observer.sig2_lineFacts.connect(m_lifetime, [this](const LineFacts &facts) {
        m_importantLog.receiveFacts(facts);
    });
    m_observer.sig2_realPrompt.connect(m_lifetime, [this]() { m_importantLog.receivePrompt(); });
    m_observer.sig2_pager.connect(m_lifetime,
                                  [this](const PagerLine &) { m_importantLog.receivePager(); });

    // The viewer setting is in MMapper.Session.State.
    m_trade.setViewerChanged([this]() { publishSessionState(); });
    m_tradeTimer.setInterval(250);
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
    // The bleeding ends when MUME has been quiet about it for a while: checked when that time
    // comes (publishCharAffects() sets the timer), as well as at the player's next event.
    m_bleedTimer.setSingleShot(true);
    connect(&m_bleedTimer, &QTimer::timeout, this, [this]() {
        if (m_charAffects.tick(QDateTime::currentSecsSinceEpoch())) {
            publishCharAffects();
        }
    });
    connect(&m_mapData, &MapData::sig_onDataChanged, this, &FrontendServer::onMapChanged);

    registerSessionHandlers();
    registerPublishers();

    // Queued on purpose: the signal is emitted while the old session is still being torn
    // down, so the slot only actually frees up once the event loop has unwound.
    connect(&m_listener,
            &ConnectionListener::sig_clientDisconnected,
            this,
            &FrontendServer::offerSession,
            Qt::QueuedConnection);
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

    if (msg.isMMapperInputQuiet()) {
        handleQuiet(*client, msg);
        return;
    }

    if (msg.isMMapperTerminalFilter()) {
        handleFilter(*client, msg);
        return;
    }

    if (msg.isMMapperSessionRememberLogin()) {
        handleRememberLogin(*client, msg);
        return;
    }

    if (msg.isMMapperTradeRequest() || msg.isMMapperTradeCancel()) {
        handleTrade(*client, msg);
        return;
    }

    if (ImportantLog::handles(msg)) {
        handleLog(*client, msg);
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
    // Whatever login was held for remembering was this frontend's.
    m_loginMemory.reset();
    // Nothing more can be sent for an operation; it stops where it is.
    m_trade.setDriving(false);
    updateViewerClaim();
    // Closing the session ends the MUME connection, exactly as closing a telnet client does.
    m_session.detach();
    log("Frontend released the session");
}

GmcpMessage FrontendServer::sessionStateFor(const Client &client) const
{
    return frontend_messages::makeSessionState(
        m_upstreamConnected,
        m_mapIdentity.get(),
        m_echo,
        m_driver != nullptr && m_driver == client.socket,
        m_observer.getGameState(),
        m_trade.viewerState(),
        m_loginPrompt,
        rememberedAccountName(getConfig().account.accountName,
                              getConfig().account.accountPassword,
                              getConfig().account.rememberLogin,
                              m_loginMemory.keychainAvailable()),
        m_loginMemory.unavailableReason());
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
    // At MUME's own login prompts, and only while the frontend asked for it, the answer is held
    // for remembering (LoginMemory); never logged, never published.
    m_loginMemory.noteSent(m_loginPrompt.kind, optText.value());
    m_session.sendLine(optText.value());
}

void FrontendServer::handleRememberLogin(Client &client, const GmcpMessage &msg)
{
    if (m_driver != client.socket) {
        sendTo(client,
               frontend_messages::makeError("read-only",
                                            "Another client owns the session; this "
                                            "connection may only observe it"));
        return;
    }
    const std::optional<bool> remember = parseRememberLogin(msg);
    if (!remember.has_value()) {
        sendTo(client,
               frontend_messages::makeError("invalid-remember",
                                            "MMapper.Session.RememberLogin needs an object payload "
                                            "with a boolean 'remember'"));
        return;
    }
    if (!m_loginMemory.setRequested(*remember)) {
        sendTo(client,
               frontend_messages::makeError("remember-unavailable",
                                            QString("MMapper cannot remember the login: %1")
                                                .arg(m_loginMemory.unavailableReason())));
        sendTo(client, sessionStateFor(client));
        return;
    }
    if (!*remember) {
        // No more auto-login; the keychain entry Preferences > General manages stays as it is.
        if (getConfig().account.rememberLogin) {
            setConfig().account.rememberLogin = false;
            getConfig().write();
            log("The frontend turned MMapper's auto-login off");
        }
        publishSessionState();
    }
}

void FrontendServer::rememberAccepted()
{
    std::optional<LoginMemory::Credentials> accepted = m_loginMemory.takeAccepted();
    if (!accepted.has_value()) {
        return;
    }
    m_storingAccount = accepted->account;
    // The keychain's key is the account's name in a web build (PasswordConfig): set it first.
    setConfig().account.accountName = accepted->account;
    m_passwordConfig->setPassword(accepted->passPhrase);
    accepted->passPhrase.fill(QLatin1Char(' '));
    accepted->passPhrase.clear();
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

    m_trade.request(payload, tradeContext());
    if (m_trade.busy() && !m_tradeTimer.isActive()) {
        m_tradeTimer.start();
    }
}

TradeOperations::Context FrontendServer::tradeContext() const
{
    TradeOperations::Context context;
    context.driving = true;
    context.connected = m_upstreamConnected;
    context.echo = m_echo;
    context.game = m_observer.getGameState();
    return context;
}

void FrontendServer::handleQuiet(Client &client, const GmcpMessage &msg)
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
               frontend_messages::makeError("invalid-quiet",
                                            "MMapper.Input.Quiet needs an object payload with "
                                            "an 'id' string"));
        return;
    }

    const bool driving = m_driver != nullptr && m_driver == client.socket;
    if (!driving) {
        // Told to the observer alone, like a trade request of its.
        QuietReplyState refused;
        refused.id = id;
        refused.text = payload.value("text").toString().left(TradeOperations::MAX_ARGUMENT_CHARS);
        refused.status = TradeStatusEnum::REFUSED;
        refused.reason = QStringLiteral("observing");
        sendTo(client, makeQuietReply(refused));
        return;
    }

    // The text itself is not logged here, as no input is.
    m_trade.requestQuiet(payload, tradeContext());
    if (m_trade.busy() && !m_tradeTimer.isActive()) {
        m_tradeTimer.start();
    }
}

void FrontendServer::handleLog(Client &client, const GmcpMessage &msg)
{
    const bool driving = m_driver != nullptr && m_driver == client.socket;
    const ImportantLog::Reply reply = m_importantLog.handle(msg, driving);
    for (const GmcpMessage &one : reply.toClient) {
        sendTo(client, one);
    }
    if (reply.rulesChanged) {
        // State: every subscriber is told the rules as they stand now.
        publish(m_importantLog.rulesMessage());
    }
}

void FrontendServer::handleFilter(Client &client, const GmcpMessage &msg)
{
    // Its own terminal output only, so an observing frontend may ask as well.
    if (!frontend_messages::applyTerminalFilter(msg, client.filter)) {
        sendTo(client,
               frontend_messages::makeError("invalid-filter",
                                            "MMapper.Terminal.Filter needs an object payload "
                                            "whose values are booleans"));
        return;
    }
    // The filter as it stands now, so that the frontend knows it was taken.
    sendTo(client, frontend_messages::makeTerminalFilter(client.filter));
}

void FrontendServer::publishTerminal(const TerminalOutput &out)
{
    if (out.roomDescription.empty()) {
        publish(frontend_messages::makeTerminalOutput(out.source, out.text, out.goAhead));
        return;
    }
    // A chunk with a room's description in it: whole for most, and built once more, without
    // it, for the frontends that asked to be spared it.
    const GmcpMessage whole = frontend_messages::makeTerminalOutput(out.source,
                                                                    out.text,
                                                                    out.goAhead);
    frontend_messages::TerminalFilter sparing;
    sparing.roomDescriptions = true;
    const std::optional<GmcpMessage> spared = frontend_messages::makeTerminalOutput(out, sparing);
    for (Client &client : m_clients) {
        if (!client.filter.roomDescriptions) {
            sendTo(client, whole);
        } else if (spared.has_value()) {
            sendTo(client, *spared);
        }
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
    // What a late frontend is told of the package is its row in FrontendReplayPolicy.cpp.
    m_replay.remember(msg);
    broadcast(msg);
}

void FrontendServer::broadcast(const GmcpMessage &msg)
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
    // The Log's rules are state too.
    sendTo(client, m_importantLog.rulesMessage());

    std::optional<GmcpMessage> position;
    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (const auto room = m_mapData.findRoomHandle(*optId)) {
            position.emplace(frontend_messages::makeMapPosition(room));
        }
    }
    m_replay.replay([&client](const GmcpMessage &msg) { sendTo(client, msg); },
                    position,
                    m_upstreamConnected);
}

void FrontendServer::publishCharAffects()
{
    publish(frontend_messages::makeCharAffects(m_charAffects.affects()));
    if (const auto endsAt = m_charAffects.bleedingEndsAt()) {
        const int64_t wait = std::max<int64_t>(*endsAt - QDateTime::currentSecsSinceEpoch(), 0);
        m_bleedTimer.start(static_cast<int>(wait * 1000 + 100));
    } else {
        m_bleedTimer.stop();
    }
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
    const GmcpMessage msg = frontend_messages::makeGroundState(ground,
                                                               room.has_value() ? &*room : nullptr);
    m_replay.hold(msg);
    broadcast(msg);
}

void FrontendServer::onRoomContents(const RoomContentsSnapshot &observed)
{
    RoomContentsSnapshot contents = observed;
    contents.snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (contents.entered) {
        m_replay.forgetContainers();
    }
    // Reported at the prompt that ends a room display, after the path machine has moved the
    // player (see onGroundChanged), so the current room is the one these objects lie in.
    std::optional<RoomHandle> room;
    if (const auto optId = m_mapData.getCurrentRoomId()) {
        if (RoomHandle handle = m_mapData.findRoomHandle(*optId)) {
            room.emplace(std::move(handle));
        }
    }
    const GmcpMessage msg = frontend_messages::makeRoomContents(contents,
                                                                room.has_value() ? &*room : nullptr);
    m_replay.hold(msg);
    broadcast(msg);
}

void FrontendServer::onItemBlock(const ItemBlock &observed)
{
    ItemBlock block = observed;
    block.snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    switch (block.kind) {
    case ItemBlockKindEnum::EQUIPMENT: {
        const GmcpMessage msg = frontend_messages::makeCharEquipment(block);
        if (block.owner == QStringLiteral("you")) {
            m_replay.hold(msg);
        }
        broadcast(msg);
        break;
    }
    case ItemBlockKindEnum::INVENTORY: {
        const GmcpMessage msg = frontend_messages::makeCharInventory(block);
        if (!block.peek) {
            m_replay.hold(msg);
        }
        broadcast(msg);
        break;
    }
    case ItemBlockKindEnum::CONTAINER: {
        const GmcpMessage msg = frontend_messages::makeCharContainer(block);
        // Without an isolated query selector the header cannot distinguish identical pouches.
        if (block.target.isEmpty()) {
            m_replay.forgetContainers();
        } else {
            // A closed reply has no location: it replaces the previous open observation for
            // this selector rather than leaving both to be replayed.
            m_replay.holdContainer(block.target, msg);
        }
        broadcast(msg);
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
    if (m_replay.has(GmcpMessageTypeEnum::MMAPPER_TIME_STATE) && !setAgain && !changed) {
        return;
    }

    const GmcpMessage msg = frontend_messages::makeTimeState(moment, precision);
    m_replay.hold(msg);
    m_timeStateMinutes = minutes;
    m_timeStateSentAt = nowSecs;
    m_timeStateHour = moment.hour;
    m_timeStatePrecision = static_cast<int>(precision);
    broadcast(msg);
}

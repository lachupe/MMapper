// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TradeOperations.h"

#include "../global/parserutils.h"
#include "../observer/gameobserver.h"
#include "FrontendSubscriptions.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace {

NODISCARD int64_t steadyNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/// A line as it went to MUME, without the newline the input path adds.
NODISCARD QString chompLine(QString line)
{
    while (!line.isEmpty()
           && (line.back() == QLatin1Char('\n') || line.back() == QLatin1Char('\r'))) {
        line.chop(1);
    }
    return line;
}

/// A frontend's argument that can go into one command line: not empty, short, and without a
/// newline or another control character that would end the line or start another.
NODISCARD std::optional<QString> argumentOf(const QJsonValue &value)
{
    if (!value.isString()) {
        return std::nullopt;
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.size() > TradeOperations::MAX_ARGUMENT_CHARS) {
        return std::nullopt;
    }
    for (const QChar c : text) {
        if (c.category() == QChar::Other_Control) {
            return std::nullopt;
        }
    }
    return text;
}

/// A whole number in [lo, hi], whether the JSON has it as an integer or a double.
NODISCARD std::optional<int64_t> integerOf(const QJsonValue &value,
                                           const int64_t lo,
                                           const int64_t hi)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double d = value.toDouble();
    const auto n = static_cast<int64_t>(d);
    if (static_cast<double>(n) != d || n < lo || n > hi) {
        return std::nullopt;
    }
    return n;
}

} // namespace

QString tradeStatusName(const TradeStatusEnum status)
{
    switch (status) {
    case TradeStatusEnum::RUNNING:
        return QStringLiteral("running");
    case TradeStatusEnum::DONE:
        return QStringLiteral("done");
    case TradeStatusEnum::REFUSED:
        return QStringLiteral("refused");
    case TradeStatusEnum::STOPPED:
        return QStringLiteral("stopped");
    case TradeStatusEnum::FAILED:
        return QStringLiteral("failed");
    case TradeStatusEnum::CANCELLED:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

GmcpMessage makeTradeOperation(const TradeOperationState &state)
{
    QJsonObject obj;
    obj["id"] = state.id;
    obj["action"] = state.action;
    obj["status"] = tradeStatusName(state.status);
    obj["step"] = state.step;
    obj["steps"] = state.steps;
    obj["reason"] = state.reason;
    obj["text"] = QJsonArray::fromStringList(state.text);
    const QByteArray json = QJsonDocument{obj}.toJson(QJsonDocument::Compact);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TRADE_OPERATION,
                       GmcpJson{QString::fromUtf8(json)}};
}

GmcpMessage makeQuietReply(const QuietReplyState &state)
{
    QJsonObject obj;
    obj["id"] = state.id;
    obj["text"] = state.text;
    obj["status"] = tradeStatusName(state.status);
    obj["reason"] = state.reason;
    obj["lines"] = QJsonArray::fromStringList(state.lines);
    obj["paged"] = state.paged;
    obj["complete"] = state.complete;
    const QByteArray json = QJsonDocument{obj}.toJson(QJsonDocument::Compact);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_INPUT_REPLY,
                       GmcpJson{QString::fromUtf8(json)}};
}

GmcpMessage makeQuietHidden(const QuietReplyState &state)
{
    QJsonObject obj;
    obj["kind"] = QStringLiteral("quiet.command");
    obj["id"] = state.id;
    obj["command"] = state.text;
    obj["count"] = static_cast<int>(state.lines.size());
    const QByteArray json = QJsonDocument{obj}.toJson(QJsonDocument::Compact);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_HIDDEN,
                       GmcpJson{QString::fromUtf8(json)}};
}

bool claimsViewer(const FrontendSubscriptions &subscriptions, const bool driving)
{
    return driving && subscriptions.subscribes("MMapper.View");
}

TradeOperations::TradeOperations(GameObserver &observer, SendFn send, PublishFn publish, NowFn now)
    : m_observer{observer}
    , m_send{std::move(send)}
    , m_publish{std::move(publish)}
    , m_now{std::move(now)}
{
    // Every line on its way to MUME, whoever sent it: the runner's own are matched against what
    // it submitted, and anything else is foreign.
    m_observer.sig2_sentToMudString.connect(m_lifetime,
                                            [this](const QString &line) { onSentToMud(line); });
    m_observer.sig2_sentToUserTerminal.connect(m_lifetime, [this](const TerminalOutput &out) {
        if (out.source != SendToUserSourceEnum::FromMud) {
            return;
        }
        // MUME is saying something; a real prompt is reported after its chunk (onPrompt).
        m_atPrompt = false;
        m_lastActivityAt = this->now(); // `now` here is the constructor's argument
        m_seenActivity = true;
        // What the terminal was shown is no part of a quiet command's reply.
        if (!out.goAhead && !quiet()) {
            onLine(out.text);
        }
    });
    // The quiet command's reply, from the parser, which kept it from the terminal.
    m_observer.sig2_quietLine.connect(m_lifetime, [this](const QString &line) {
        if (quiet() && m_op->replying) {
            onQuietLine(line);
        }
    });
    m_observer.sig2_quietEnded.connect(m_lifetime, [this]() {
        if (quiet() && m_op->replying) {
            m_op->ended = true;
        }
    });
    m_observer.sig2_pager.connect(m_lifetime, [this](const PagerLine &pager) { onPager(pager); });
    m_observer.sig2_realPrompt.connect(m_lifetime, [this]() { onPrompt(); });

    // The readers' packages, kept for the step whose reply is being read.
    m_observer.sig2_shopList.connect(m_lifetime, [this](const ShopList &list) {
        if (m_op.has_value() && m_op->replying) {
            m_op->list = list;
        }
    });
    m_observer.sig2_shopDeal.connect(m_lifetime, [this](const ShopDeal &deal) {
        if (m_op.has_value() && m_op->replying) {
            m_op->deal = deal;
        }
    });
    m_observer.sig2_guildTeacher.connect(m_lifetime, [this](const GuildTeacher &teacher) {
        if (teacher.sessionsLeft.has_value()) {
            m_lastSessionsLeft = teacher.sessionsLeft;
        }
        if (m_op.has_value() && m_op->replying) {
            m_op->teacher = teacher;
        }
    });
    m_observer.sig2_guildPractised.connect(m_lifetime, [this](const GuildPractised &practised) {
        if (m_op.has_value() && m_op->replying) {
            m_op->practised = practised;
        }
    });
    m_observer.sig2_charSkills.connect(m_lifetime, [this](const CharSkills &skills) {
        if (skills.sessionsLeft.has_value()) {
            m_lastSessionsLeft = skills.sessionsLeft;
        }
        if (m_op.has_value() && m_op->replying) {
            m_op->skills = true;
        }
    });
    m_observer.sig2_innOffer.connect(m_lifetime, [this](const InnOffer &offer) {
        if (m_op.has_value() && m_op->replying) {
            m_op->offer = offer;
        }
    });
    m_observer.sig2_charTrophies.connect(m_lifetime, [this](const CharTrophies &) {
        if (m_op.has_value() && m_op->replying) {
            m_op->trophies = true;
        }
    });

    m_observer.sig2_connected.connect(m_lifetime, [this]() {
        abort(QStringLiteral("disconnected"));
        resetConnection();
    });
    m_observer.sig2_disconnected.connect(m_lifetime, [this]() {
        abort(QStringLiteral("disconnected"));
        resetConnection();
    });
    m_observer.sig2_toggledEchoMode.connect(m_lifetime, [this](const bool echo) {
        m_echo = echo;
        // Lines sent while MUME echoes nothing are not reported, so what was in flight can no
        // longer be matched.
        m_sent.clear();
        abort(QStringLiteral("echo changed"));
    });
    m_observer.sig2_gameStateChanged.connect(m_lifetime, [this](const GameStateEnum state) {
        if (state != GameStateEnum::PLAYING) {
            // A new login sets the viewer again, unless the player set it in this connection.
            m_viewerSent = false;
        }
        if (!m_op.has_value()) {
            return;
        }
        // The rent that inn.rent, and the second step of inn.retire, are for.
        const bool wanted = m_op->replying && state != GameStateEnum::PLAYING
                            && (m_op->kind == KindEnum::INN_RENT
                                || (m_op->kind == KindEnum::INN_RETIRE && m_op->state.step == 2));
        if (wanted) {
            finish(TradeStatusEnum::DONE, QString{}, false);
        } else {
            abort(QStringLiteral("left the game"));
        }
    });
}

TradeOperations::~TradeOperations()
{
    if (quiet() && m_op->replying) {
        // Nobody is left to answer a pager the parser would hide.
        m_observer.observeQuietCommand(QuietCommandEnum::END);
    }
}

int64_t TradeOperations::now() const
{
    return m_now ? m_now() : steadyNowMs();
}

void TradeOperations::setDriving(const bool driving)
{
    if (driving == m_driving) {
        return;
    }
    m_driving = driving;
    if (!driving) {
        abort(QStringLiteral("session released"));
        // Whatever was submitted through the old session will never be reported.
        m_sent.clear();
    }
}

void TradeOperations::resetConnection()
{
    m_sent.clear();
    m_pagerOpen = false;
    m_pagerOwned = false;
    m_atPrompt = false;
    m_owed = 0;
    m_lastSessionsLeft.reset();
    m_viewerSent = false;
    m_viewerSetByPlayer = false;
    if (m_viewer != QStringLiteral("unknown")) {
        m_viewer = QStringLiteral("unknown");
        if (m_viewerChanged) {
            m_viewerChanged();
        }
    }
}

void TradeOperations::refuse(const QString &id, const QString &action, const QString &reason)
{
    TradeOperationState state;
    state.id = id;
    state.action = action;
    state.status = TradeStatusEnum::REFUSED;
    state.reason = reason;
    m_publish(makeTradeOperation(state));
}

void TradeOperations::request(const QJsonObject &payload, const Context &context)
{
    const QString id = payload.value("id").toString();
    const QString action = payload.value("action").toString();
    const QString invalid = QStringLiteral("invalid arguments");

    Operation op;
    op.state.id = id;
    op.state.action = action;
    op.state.steps = 1;

    if (action == QStringLiteral("shop.list")) {
        op.kind = KindEnum::SHOP_LIST;
        const QJsonValue filter = payload.value("filter");
        if (filter.isUndefined() || filter.isNull()
            || (filter.isString() && filter.toString().trimmed().isEmpty())) {
            op.commands << QStringLiteral("list");
        } else if (const auto words = argumentOf(filter)) {
            op.commands << QStringLiteral("list ") + *words;
        } else {
            return refuse(id, action, invalid);
        }
    } else if (action == QStringLiteral("shop.buy")) {
        op.kind = KindEnum::SHOP_BUY;
        const auto number = integerOf(payload.value("number"), 1, 999999);
        const QJsonValue countValue = payload.value("count");
        const auto count = (countValue.isUndefined() || countValue.isNull())
                               ? std::optional<int64_t>{1}
                               : integerOf(countValue, 1, MAX_BUY);
        if (!number.has_value() || !count.has_value()) {
            return refuse(id, action, invalid);
        }
        op.commands << ((*count == 1) ? QStringLiteral("buy %1").arg(*number)
                                      : QStringLiteral("buy %1 %2").arg(*count).arg(*number));
    } else if (action == QStringLiteral("shop.sell")) {
        op.kind = KindEnum::SHOP_SELL;
        const QJsonValue items = payload.value("items");
        if (!items.isArray() || items.toArray().isEmpty()
            || items.toArray().size() > MAX_SELL_ITEMS) {
            return refuse(id, action, invalid);
        }
        for (const QJsonValue &item : items.toArray()) {
            const auto selector = argumentOf(item);
            if (!selector.has_value()) {
                return refuse(id, action, invalid);
            }
            op.commands << QStringLiteral("sell ") + *selector;
        }
        op.state.steps = static_cast<int>(op.commands.size());
    } else if (action == QStringLiteral("guild.list")) {
        op.kind = KindEnum::GUILD_LIST;
        op.commands << QStringLiteral("prac");
    } else if (action == QStringLiteral("guild.practise")) {
        op.kind = KindEnum::GUILD_PRACTISE;
        const auto name = argumentOf(payload.value("name"));
        const QJsonValue times = payload.value("times");
        if (!name.has_value()) {
            return refuse(id, action, invalid);
        }
        if (times.isString() && times.toString() == QStringLiteral("limit")) {
            op.untilLimit = true;
            op.state.steps = 0;
        } else if (const auto n = integerOf(times, 1, MAX_PRACTISE)) {
            op.state.steps = static_cast<int>(*n);
        } else {
            return refuse(id, action, invalid);
        }
        op.commands << QStringLiteral("prac ") + *name;
        op.sessionsLeft = m_lastSessionsLeft;
    } else if (action == QStringLiteral("inn.offer")) {
        op.kind = KindEnum::INN_OFFER;
        op.commands << QStringLiteral("offer");
    } else if (action == QStringLiteral("inn.rent")) {
        op.kind = KindEnum::INN_RENT;
        op.commands << QStringLiteral("rent");
    } else if (action == QStringLiteral("inn.retire")) {
        op.kind = KindEnum::INN_RETIRE;
        op.commands << QStringLiteral("rent retire") << QStringLiteral("rent retire");
        op.state.steps = 2;
    } else if (action == QStringLiteral("char.trophies")) {
        op.kind = KindEnum::CHAR_TROPHIES;
        op.commands << QStringLiteral("trop");
    } else {
        // shop.pieces, shop.show, shop.value, shop.mend, shop.resize wait for a live session
        // to show their replies; anything else is not an action at all.
        return refuse(id, action, QStringLiteral("unsupported"));
    }

    if (!context.connected) {
        return refuse(id, action, QStringLiteral("not connected"));
    }
    if (context.game != GameStateEnum::PLAYING) {
        return refuse(id, action, QStringLiteral("not in the game"));
    }
    if (!context.driving || !m_driving) {
        return refuse(id, action, QStringLiteral("observing"));
    }
    if (!context.echo || !m_echo) {
        return refuse(id, action, QStringLiteral("echo off"));
    }
    if (m_op.has_value()) {
        return refuse(id, action, QStringLiteral("busy"));
    }
    if (m_pagerOpen) {
        // The player's own reply is at a pager: whatever the runner sent would answer it.
        return refuse(id, action, QStringLiteral("pager open"));
    }

    op.serial = ++m_serial;
    m_op.emplace(std::move(op));
    startStep();
}

void TradeOperations::requestQuiet(const QJsonObject &payload, const Context &context)
{
    QuietReplyState refused;
    refused.id = payload.value("id").toString();
    refused.status = TradeStatusEnum::REFUSED;
    const auto refuse = [this, &refused](const char *const reason) {
        refused.reason = QString::fromLatin1(reason);
        m_publish(makeQuietReply(refused));
    };

    // A string no longer than a trade request's argument; and then one of the commands that
    // only read, alone on its line. A command the player never sees must not act, whatever a
    // frontend asks: anything else is refused whole, and nothing of it is sent.
    const QJsonValue textValue = payload.value("text");
    const QString text = textValue.toString().trimmed();
    refused.text = text.left(MAX_ARGUMENT_CHARS);
    if (!textValue.isString() || text.size() > MAX_ARGUMENT_CHARS) {
        return refuse("invalid arguments");
    }
    if (!quietCommandAllowed(text)) {
        return refuse("not allowed");
    }
    if (!context.connected) {
        return refuse("not connected");
    }
    if (context.game != GameStateEnum::PLAYING) {
        return refuse("not in the game");
    }
    if (!context.driving || !m_driving) {
        return refuse("observing");
    }
    if (!context.echo || !m_echo) {
        return refuse("echo off");
    }
    if (m_op.has_value()) {
        return refuse("busy");
    }
    if (m_pagerOpen) {
        // The player's own reply is at a pager: the command would answer it.
        return refuse("pager open");
    }

    Operation op;
    op.kind = KindEnum::QUIET;
    op.state.id = refused.id;
    op.state.action = text;
    op.commands << text;
    op.waiting = true;
    op.deadline = now() + QUIET_WAIT_MS;
    op.serial = ++m_serial;
    m_op.emplace(std::move(op));
    sendQuietWhenIdle();
}

bool TradeOperations::idle() const
{
    if (m_pagerOpen) {
        return false;
    }
    const int64_t at = now();
    // Nothing sent and nothing said for a while: whatever was owed is not coming.
    if (!m_seenActivity || at - m_lastActivityAt >= QUIET_IDLE_MS) {
        return true;
    }
    // Every line sent has had its prompt, and the last thing MUME sent was one.
    return m_atPrompt && m_owed == 0 && at - m_lastSentAt >= QUIET_SETTLE_MS;
}

void TradeOperations::sendQuietWhenIdle()
{
    if (!quiet() || !m_op->waiting || !idle()) {
        return;
    }
    m_op->waiting = false;
    m_op->deadline = now() + QUIET_TIMEOUT_MS;
    submit(m_op->commands.front(), SentEnum::STEP);
}

void TradeOperations::submit(const QString &line, const SentEnum kind)
{
    m_sent.push_back(Sent{line, kind, m_op.has_value() ? m_op->serial : 0});
    m_send(line);
}

void TradeOperations::startStep()
{
    assert(m_op.has_value());
    Operation &op = *m_op;
    ++op.state.step;
    op.replying = false;
    op.deadline = now() + STEP_TIMEOUT_MS;
    op.stepText.clear();
    op.stepLines = 0;
    op.lastPercent = -1;
    op.pages = 0;
    op.linesSincePager = 0;
    op.pageHeight = 0;
    op.list.reset();
    op.deal.reset();
    op.teacher.reset();
    op.practised.reset();
    op.skills = false;
    op.offer.reset();
    op.trophies = false;

    const auto index = static_cast<qsizetype>(op.state.step - 1);
    const QString command = (index < op.commands.size()) ? op.commands.at(index)
                                                         : op.commands.back();
    op.state.status = TradeStatusEnum::RUNNING;
    op.state.reason.clear();
    op.state.text.clear();
    publishState();
    submit(command, SentEnum::STEP);
}

void TradeOperations::publishState()
{
    if (m_op.has_value()) {
        m_publish(makeTradeOperation(m_op->state));
    }
}

void TradeOperations::finish(const TradeStatusEnum status,
                             const QString &reason,
                             const bool withText)
{
    if (!m_op.has_value()) {
        return;
    }
    Operation op = std::move(*m_op);
    m_op.reset();
    if (op.kind == KindEnum::QUIET) {
        // Over: the parser hides nothing more. Told before the reply goes out, so that whoever
        // hears the reply finds the terminal as it will stay.
        if (op.replying) {
            m_observer.observeQuietCommand(QuietCommandEnum::END);
        } else if (!op.waiting) {
            // Submitted and never seen going to MUME (a mapper command, a line MMapper
            // rewrote): it must not stand in the way of matching what is sent later.
            std::erase_if(m_sent, [&op](const Sent &sent) {
                return sent.kind == SentEnum::STEP && sent.serial == op.serial;
            });
        }
        QuietReplyState reply;
        reply.id = op.state.id;
        reply.text = op.commands.front();
        reply.status = status;
        reply.reason = reason;
        reply.lines = op.stepText;
        while (!reply.lines.isEmpty() && reply.lines.back().trimmed().isEmpty()) {
            reply.lines.removeLast();
        }
        reply.paged = op.pages > 0;
        reply.complete = status == TradeStatusEnum::DONE && !op.truncated;
        // What was kept from the terminal is said once, for whoever lists such things, before
        // the reply that carries it.
        if (!reply.lines.isEmpty()) {
            m_publish(makeQuietHidden(reply));
        }
        m_publish(makeQuietReply(reply));
        return;
    }
    op.state.status = status;
    op.state.reason = reason;
    op.state.text = withText ? op.stepText : QStringList{};
    m_publish(makeTradeOperation(op.state));
}

void TradeOperations::abort(const QString &reason)
{
    finish(TradeStatusEnum::STOPPED, reason, true);
}

void TradeOperations::cancel(const QString &id)
{
    if (!m_op.has_value() || m_op->state.id != id || m_op->kind == KindEnum::QUIET) {
        return; // Not running: its last status has already gone out.
    }
    if (m_pagerOpen && m_pagerOwned) {
        submit(QStringLiteral("q"), SentEnum::QUIT);
    }
    finish(TradeStatusEnum::CANCELLED, QStringLiteral("cancelled"), false);
}

void TradeOperations::beforePlayerLine(const QString & /*line*/)
{
    if (!m_op.has_value()) {
        return;
    }
    // The player's line always goes, and at once; a pager the runner holds would take it as its
    // answer, so it is quit first.
    if (m_pagerOpen && m_pagerOwned) {
        submit(QStringLiteral("q"), SentEnum::QUIT);
    } else if (m_op->kind == KindEnum::QUIET) {
        // MUME answers in order: the player's reply comes after the quiet command's prompt,
        // or, while the command still waits, before it is sent at all. It goes on.
        return;
    }
    abort(QStringLiteral("player input"));
}

void TradeOperations::expire()
{
    if (quiet() && m_op->waiting) {
        if (now() >= m_op->deadline) {
            finish(TradeStatusEnum::REFUSED, QStringLiteral("not idle"), false);
        } else {
            sendQuietWhenIdle();
        }
        return;
    }
    if (!m_op.has_value() || now() < m_op->deadline) {
        return;
    }
    if (m_pagerOpen && m_pagerOwned) {
        submit(QStringLiteral("q"), SentEnum::QUIT);
    }
    finish(TradeStatusEnum::FAILED, QStringLiteral("timeout"), true);
}

void TradeOperations::onSentToMud(const QString &raw)
{
    const QString line = chompLine(raw);
    // Whoever sent it, the first line after a pager is its answer, and has no prompt of its
    // own; every other line has one to come.
    if (!m_pagerOpen) {
        ++m_owed;
    }
    m_pagerOpen = false;
    m_pagerOwned = false;
    m_atPrompt = false;
    m_lastSentAt = now();
    m_lastActivityAt = m_lastSentAt;
    m_seenActivity = true;

    if (!m_sent.empty() && m_sent.front().line == line) {
        const Sent sent = m_sent.front();
        m_sent.pop_front();
        if (sent.kind == SentEnum::STEP && m_op.has_value() && m_op->serial == sent.serial) {
            // The step's command has reached MUME: what comes back now is its reply.
            m_op->replying = true;
            m_op->deadline = now() + STEP_TIMEOUT_MS;
            if (m_op->kind == KindEnum::QUIET) {
                m_op->deadline = now() + QUIET_TIMEOUT_MS;
                if (m_op->raced) {
                    // Another reply comes first, and it is not this command's: nothing is
                    // hidden, and MUME's answer to the command is shown like any other.
                    m_op->replying = false;
                    finish(TradeStatusEnum::FAILED, QStringLiteral("overlapping command"), false);
                    return;
                }
                // From here on the parser keeps the reply from the terminal.
                m_observer.observeQuietCommand(QuietCommandEnum::BEGIN);
            }
        }
        return;
    }

    // Foreign: the player's, an alias's, a frontend's quiet refresh.
    noteViewerLine(line);
    if (!m_op.has_value()) {
        return;
    }
    if (m_op->kind != KindEnum::QUIET) {
        abort(QStringLiteral("overlapping command"));
    } else if (m_op->replying) {
        // Its reply follows the quiet command's prompt: the window must not outlast that.
        m_observer.observeQuietCommand(QuietCommandEnum::FOREIGN);
    } else if (!m_op->waiting) {
        m_op->raced = true;
    }
}

void TradeOperations::onLine(const QString &chunk)
{
    if (!m_op.has_value() || !m_op->replying) {
        return;
    }
    QString plain = chunk;
    ParserUtils::removeAnsiMarksInPlace(plain);
    QStringList lines = plain.split(QLatin1Char('\n'));
    // A chunk ends in its newline, which leaves an empty piece after it.
    if (lines.size() > 1 && lines.back().isEmpty()) {
        lines.removeLast();
    }
    for (const QString &line : lines) {
        ++m_op->stepLines;
        ++m_op->linesSincePager;
        if (m_op->stepText.size() < MAX_TEXT_LINES) {
            m_op->stepText << chompLine(line).left(MAX_TEXT_CHARS);
        }
    }
}

void TradeOperations::onQuietLine(const QString &raw)
{
    Operation &op = *m_op;
    const QString line = chompLine(raw);
    ++op.stepLines;
    ++op.linesSincePager;
    if (!line.trimmed().isEmpty()) {
        ++op.replyLines;
    } else if (op.replyLines == 0) {
        return; // The spacing before the reply, or before whatever else MUME said.
    }
    if (op.stepText.size() < MAX_QUIET_LINES) {
        op.stepText << line;
    } else {
        op.truncated = true;
    }
}

void TradeOperations::onPager(const PagerLine &pager)
{
    m_pagerOpen = true;
    m_atPrompt = false;
    // A quiet command owns the pager the parser hid and no other: one the player sees is in a
    // reply of the player's.
    m_pagerOwned = m_op.has_value() && m_op->replying
                   && (m_op->kind != KindEnum::QUIET || pager.hidden);
    if (!m_pagerOwned) {
        return; // The player's reply: the player's pager.
    }
    Operation &op = *m_op;
    if (op.pages == 0 && op.lastPercent < 0) {
        op.pageHeight = op.stepLines;
    }
    // MUME shows the pager again, with the same percentage, after other output: that one was
    // answered already. A new percentage, or a whole page of lines since, is a new page.
    const bool fullPage = op.pageHeight >= 5 && op.linesSincePager >= op.pageHeight - 1;
    const bool newPage = pager.percent != op.lastPercent || fullPage;
    if (!newPage) {
        return;
    }
    if (op.pages >= MAX_PAGES) {
        submit(QStringLiteral("q"), SentEnum::QUIT);
        abort(QStringLiteral("too many pages"));
        return;
    }
    ++op.pages;
    op.lastPercent = pager.percent;
    op.linesSincePager = 0;
    op.deadline = now() + (op.kind == KindEnum::QUIET ? QUIET_TIMEOUT_MS : STEP_TIMEOUT_MS);
    submit(QString{}, SentEnum::PAGER);
}

TradeOperations::Outcome TradeOperations::judge() const
{
    const Operation &op = *m_op;
    const auto dealRefusal = [](const ShopDeal &deal) -> QString {
        switch (deal.kind) {
        case ShopDealKindEnum::MISS:
            return QStringLiteral("no such thing");
        case ShopDealKindEnum::CLOSED:
            return QStringLiteral("closed");
        case ShopDealKindEnum::REFUSED:
        case ShopDealKindEnum::VALUE:
        case ShopDealKindEnum::BUY:
        case ShopDealKindEnum::SELL:
            break;
        }
        return QStringLiteral("refused");
    };

    switch (op.kind) {
    case KindEnum::SHOP_LIST:
        if (op.list.has_value()) {
            return {OutcomeEnum::DONE, {}};
        }
        if (op.deal.has_value()) {
            // "There is no such thing for sale." answers a list as well: nothing matched.
            if (op.deal->kind == ShopDealKindEnum::MISS) {
                return {OutcomeEnum::DONE, {}};
            }
            return {OutcomeEnum::REFUSED, dealRefusal(*op.deal)};
        }
        break;
    case KindEnum::SHOP_BUY:
    case KindEnum::SHOP_SELL:
        if (op.deal.has_value()) {
            const auto wanted = (op.kind == KindEnum::SHOP_BUY) ? ShopDealKindEnum::BUY
                                                                : ShopDealKindEnum::SELL;
            if (op.deal->kind == wanted) {
                return {OutcomeEnum::NEXT, {}};
            }
            return {OutcomeEnum::REFUSED, dealRefusal(*op.deal)};
        }
        break;
    case KindEnum::GUILD_LIST:
        if (op.teacher.has_value() || op.skills) {
            return {OutcomeEnum::DONE, {}};
        }
        break;
    case KindEnum::GUILD_PRACTISE:
        if (op.practised.has_value()) {
            if (!op.practised->refused.isEmpty()) {
                return {OutcomeEnum::REFUSED, QStringLiteral("refused")};
            }
            const auto &p = *op.practised;
            if (p.used.has_value() && p.most.has_value() && *p.used >= *p.most) {
                return {OutcomeEnum::DONE, QStringLiteral("limit reached")};
            }
            if (op.sessionsLeft.has_value() && *op.sessionsLeft - 1 <= 0) {
                return {OutcomeEnum::DONE, QStringLiteral("limit reached")};
            }
            return {OutcomeEnum::NEXT, {}};
        }
        break;
    case KindEnum::INN_OFFER:
        if (op.offer.has_value()) {
            return {OutcomeEnum::DONE, {}};
        }
        break;
    case KindEnum::INN_RENT:
        // Done when the character leaves the game (sig2_gameStateChanged); a reply without it
        // is a rent that did not happen.
        if (op.stepLines > 0) {
            return {OutcomeEnum::FAILED, QStringLiteral("not rented")};
        }
        break;
    case KindEnum::INN_RETIRE:
        if (op.state.step == 1) {
            if (op.offer.has_value() && op.offer->retireAsksRepeat) {
                return {OutcomeEnum::NEXT, {}};
            }
        } else if (op.stepLines > 0) {
            return {OutcomeEnum::FAILED, QStringLiteral("not rented")};
        }
        break;
    case KindEnum::CHAR_TROPHIES:
        if (op.trophies) {
            return {OutcomeEnum::DONE, {}};
        }
        break;
    case KindEnum::QUIET:
        // Judged by the parser's window alone: the prompt that closed it ends the reply, and
        // every prompt before that is of something else.
        if (!op.ended) {
            return {OutcomeEnum::WAIT, {}};
        }
        if (op.replyLines > 0) {
            return {OutcomeEnum::DONE, {}};
        }
        return {OutcomeEnum::FAILED, QStringLiteral("no reply")};
    }

    // No recognised reply. A prompt before any line of the reply was already on its way when
    // the command went; the step waits for the next one.
    if (op.stepLines == 0) {
        return {OutcomeEnum::WAIT, {}};
    }
    return {OutcomeEnum::FAILED, QStringLiteral("no reply")};
}

void TradeOperations::onPrompt()
{
    m_pagerOpen = false;
    m_pagerOwned = false;
    m_atPrompt = true;
    if (m_owed > 0) {
        --m_owed;
    }

    if (m_op.has_value() && m_op->replying) {
        const Outcome outcome = judge();
        Operation &op = *m_op;
        switch (outcome.kind) {
        case OutcomeEnum::WAIT:
            break;
        case OutcomeEnum::NEXT: {
            if (op.sessionsLeft.has_value()) {
                --*op.sessionsLeft;
            }
            const bool more = op.untilLimit ? op.state.step < MAX_PRACTISE
                                            : op.state.step < op.state.steps;
            if (more) {
                startStep();
            } else {
                finish(TradeStatusEnum::DONE, QString{}, false);
            }
            break;
        }
        case OutcomeEnum::DONE:
            finish(TradeStatusEnum::DONE, outcome.reason, false);
            break;
        case OutcomeEnum::REFUSED:
            if (op.state.step == 1) {
                finish(TradeStatusEnum::REFUSED, outcome.reason, true);
            } else if (op.kind == KindEnum::GUILD_PRACTISE && op.untilLimit) {
                // Practising as far as the teacher goes ends at the teacher's refusal.
                finish(TradeStatusEnum::DONE, QStringLiteral("limit reached"), false);
            } else {
                finish(TradeStatusEnum::STOPPED, outcome.reason, true);
            }
            break;
        case OutcomeEnum::FAILED:
            finish(TradeStatusEnum::FAILED, outcome.reason, true);
            break;
        }
    }

    maybeSetViewer();
    sendQuietWhenIdle();
}

void TradeOperations::maybeSetViewer()
{
    // Once per login, at a prompt where nothing else is being said to MUME.
    if (m_viewerSent || m_viewerSetByPlayer || m_op.has_value() || m_pagerOpen || !m_driving
        || !m_echo || !m_observer.isViewerClaimed()
        || m_observer.getGameState() != GameStateEnum::PLAYING) {
        return;
    }
    m_viewerSent = true;
    submit(QStringLiteral("change viewer external"), SentEnum::VIEWER);
    if (m_viewer != QStringLiteral("external")) {
        m_viewer = QStringLiteral("external");
        if (m_viewerChanged) {
            m_viewerChanged();
        }
    }
}

void TradeOperations::noteViewerLine(const QString &line)
{
    static const QRegularExpression re{QStringLiteral(R"(^\s*cha\w*\s+vie\w*\s+(\w+))"),
                                       QRegularExpression::CaseInsensitiveOption};
    const auto match = re.match(line);
    if (!match.hasMatch()) {
        return;
    }
    const QString value = match.captured(1).toLower();
    QString viewer;
    for (const char *const option : {"external", "simple", "off"}) {
        if (QString::fromLatin1(option).startsWith(value)) {
            viewer = QString::fromLatin1(option);
            break;
        }
    }
    if (viewer.isEmpty()) {
        return; // Not a setting MUME knows; it answers with an error and changes nothing.
    }
    // The player's own choice, respected for the rest of this connection.
    m_viewerSetByPlayer = true;
    if (m_viewer != viewer) {
        m_viewer = viewer;
        if (m_viewerChanged) {
            m_viewerChanged();
        }
    }
}

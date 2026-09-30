#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/RuleOf5.h"
#include "../global/Signal2.h"
#include "../global/macros.h"
#include "../parser/GameStateLines.h"
#include "../parser/TradeLines.h"
#include "../proxy/GmcpMessage.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

#include <QJsonObject>
#include <QString>
#include <QStringList>

class FrontendSubscriptions;
class GameObserver;

/// What a trade operation is doing, as MMapper.Trade.Operation's `status` says.
enum class NODISCARD TradeStatusEnum : uint8_t {
    RUNNING,
    DONE,
    REFUSED,
    STOPPED,
    FAILED,
    CANCELLED
};

NODISCARD QString tradeStatusName(TradeStatusEnum status);

/// One MMapper.Trade.Operation, as published.
struct NODISCARD TradeOperationState final
{
    QString id;
    QString action;
    TradeStatusEnum status = TradeStatusEnum::RUNNING;
    int step = 0;
    int steps = 0;
    QString reason;
    QStringList text;
};

/// MMapper.Trade.Operation for `state`.
NODISCARD GmcpMessage makeTradeOperation(const TradeOperationState &state);

/// True if a frontend with these subscriptions claims MUME's viewer: only the driving frontend
/// does, and only while it subscribes to MMapper.View.
NODISCARD bool claimsViewer(const FrontendSubscriptions &subscriptions, bool driving);

/// Runs the short conversations with MUME that MMapper.Trade.Request asks for: sends each
/// step's command through the driving frontend's input path, answers MUME's pager for its own
/// replies, judges each step at the next real prompt by the readers' packages (TradeLines),
/// and publishes MMapper.Trade.Operation. One operation at a time overall.
///
/// It also keeps MUME's viewer setting: `change viewer external` once per login while the
/// driving frontend claims the viewer, unless the player set the viewer themselves.
///
/// Owned by FrontendServer; everything it hears comes from GameObserver, so it can be driven
/// by a scripted MUME in tests.
class NODISCARD TradeOperations final
{
public:
    using SendFn = std::function<void(const QString &)>;
    using PublishFn = std::function<void(const GmcpMessage &)>;
    /// Milliseconds on a clock that only moves forward.
    using NowFn = std::function<int64_t()>;
    using ChangedFn = std::function<void()>;

    static constexpr int64_t STEP_TIMEOUT_MS = 15000;
    static constexpr int MAX_PAGES = 20;
    static constexpr int MAX_BUY = 20;
    static constexpr int MAX_SELL_ITEMS = 50;
    static constexpr int MAX_PRACTISE = 100;
    static constexpr int MAX_TEXT_LINES = 32;
    static constexpr int MAX_TEXT_CHARS = 1024;
    static constexpr int MAX_ARGUMENT_CHARS = 100;
    static constexpr int MAX_ID_CHARS = 128;

    /// What FrontendServer knows about the session when a request comes.
    struct NODISCARD Context final
    {
        bool driving = false;
        bool connected = false;
        bool echo = true;
        GameStateEnum game = GameStateEnum::UNKNOWN;
    };

private:
    enum class NODISCARD KindEnum : uint8_t {
        SHOP_LIST,
        SHOP_BUY,
        SHOP_SELL,
        GUILD_LIST,
        GUILD_PRACTISE,
        INN_OFFER,
        INN_RENT,
        INN_RETIRE,
        CHAR_TROPHIES
    };
    enum class NODISCARD SentEnum : uint8_t { STEP, PAGER, QUIT, VIEWER };
    struct NODISCARD Sent final
    {
        QString line;
        SentEnum kind = SentEnum::STEP;
        uint64_t serial = 0;
    };
    enum class NODISCARD OutcomeEnum : uint8_t { WAIT, NEXT, DONE, REFUSED, FAILED };
    struct NODISCARD Outcome final
    {
        OutcomeEnum kind = OutcomeEnum::WAIT;
        QString reason;
    };
    struct NODISCARD Operation final
    {
        TradeOperationState state;
        KindEnum kind = KindEnum::SHOP_LIST;
        uint64_t serial = 0;
        /// The commands of a fixed list of steps (shop.sell), or the one repeated command.
        QStringList commands;
        bool untilLimit = false;
        std::optional<int64_t> sessionsLeft;

        /// Its current step's command has reached MUME, and its reply is being read.
        bool replying = false;
        int64_t deadline = 0;
        /// Plain lines of the current step's reply, and how many.
        QStringList stepText;
        int stepLines = 0;
        /// The pager of the current step's reply.
        int lastPercent = -1;
        int pages = 0;
        int linesSincePager = 0;
        int pageHeight = 0;

        /// The readers' packages seen in the current step's window.
        std::optional<ShopList> list;
        std::optional<ShopDeal> deal;
        std::optional<GuildTeacher> teacher;
        std::optional<GuildPractised> practised;
        bool skills = false;
        std::optional<InnOffer> offer;
        bool trophies = false;
    };

private:
    GameObserver &m_observer;
    SendFn m_send;
    PublishFn m_publish;
    NowFn m_now;
    ChangedFn m_viewerChanged;
    Signal2Lifetime m_lifetime;

    std::optional<Operation> m_op;
    uint64_t m_serial = 0;
    /// What was submitted and has not been seen going to MUME yet, oldest first.
    std::deque<Sent> m_sent;

    bool m_driving = false;
    bool m_echo = true;
    /// A pager line was shown and nothing has gone to MUME since; owned if in the runner's reply.
    bool m_pagerOpen = false;
    bool m_pagerOwned = false;
    /// The last teacher's table seen, for the sessions left when practising starts.
    std::optional<int64_t> m_lastSessionsLeft;

    QString m_viewer = QStringLiteral("unknown");
    bool m_viewerSent = false;
    bool m_viewerSetByPlayer = false;

public:
    explicit TradeOperations(GameObserver &observer, SendFn send, PublishFn publish, NowFn now = {});
    ~TradeOperations();
    DELETE_CTORS_AND_ASSIGN_OPS(TradeOperations);

public:
    /// Called when MUME's viewer setting as MMapper knows it changes (Session.State's `viewer`).
    void setViewerChanged(ChangedFn fn) { m_viewerChanged = std::move(fn); }

    /// Starts the operation `payload` asks for, or refuses it; publishes its Operation either
    /// way. The payload's `id` must already be checked to be a non-empty string.
    void request(const QJsonObject &payload, const Context &context);
    /// Cancels the running operation if it has this id; ignored otherwise.
    void cancel(const QString &id);
    /// The driving frontend is about to send `line` of its own: a pager the runner holds is
    /// answered with `q` first, and the running operation stops.
    void beforePlayerLine(const QString &line);
    /// Stops the running operation without sending anything more.
    void abort(const QString &reason);
    /// Fails the running step if its time is up. FrontendServer calls it every second.
    void expire();
    /// Whether a frontend is driving, which is what lets the runner send anything.
    void setDriving(bool driving);

public:
    NODISCARD QString viewerState() const { return m_viewer; }
    NODISCARD bool busy() const { return m_op.has_value(); }
    NODISCARD bool pagerOpen() const { return m_pagerOpen; }

private:
    NODISCARD int64_t now() const;
    void submit(const QString &line, SentEnum kind);
    void startStep();
    void finish(TradeStatusEnum status, const QString &reason, bool withText);
    void publishState();
    void refuse(const QString &id, const QString &action, const QString &reason);
    NODISCARD Outcome judge() const;
    void onPrompt();
    void onSentToMud(const QString &line);
    void onPager(const PagerLine &pager);
    void onLine(const QString &text);
    void maybeSetViewer();
    void noteViewerLine(const QString &line);
    void resetConnection();
};

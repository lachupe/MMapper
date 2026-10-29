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

/// One MMapper.Input.Reply: the answer to an MMapper.Input.Quiet, sent once, when it is over.
struct NODISCARD QuietReplyState final
{
    QString id;
    /// The command, as it was asked for.
    QString text;
    /// DONE, REFUSED, STOPPED or FAILED.
    TradeStatusEnum status = TradeStatusEnum::DONE;
    QString reason;
    /// What MUME answered, colour removed, one line per line; what had come when it ended
    /// early.
    QStringList lines;
    /// MUME's pager cut the reply, and MMapper answered it.
    bool paged = false;
    /// The reply came whole, to its prompt.
    bool complete = false;
};

/// MMapper.Input.Reply for `state`.
NODISCARD GmcpMessage makeQuietReply(const QuietReplyState &state);

/// MMapper.Terminal.Hidden for the quiet command of `state`: `kind` "quiet.command", `id`,
/// `command` and `count`, how many lines were kept from the terminal. The lines themselves are
/// in the MMapper.Input.Reply that follows, and are not sent twice.
NODISCARD GmcpMessage makeQuietHidden(const QuietReplyState &state);

/// True if a frontend with these subscriptions claims MUME's viewer: only the driving frontend
/// does, and only while it subscribes to MMapper.View.
NODISCARD bool claimsViewer(const FrontendSubscriptions &subscriptions, bool driving);

/// Runs the short conversations with MUME that MMapper.Trade.Request asks for: sends each
/// step's command through the driving frontend's input path, answers MUME's pager for its own
/// replies, judges each step at the next real prompt by the readers' packages (TradeLines),
/// and publishes MMapper.Trade.Operation. One operation at a time overall.
///
/// The quiet command, MMapper.Input.Quiet, is run here as well, since it shares everything that
/// matters with those conversations: one thing at a time said to MUME on MMapper's own account,
/// the list of what was sent, the pager. It is one line of the frontend's choosing, whose reply
/// goes to the frontend (MMapper.Input.Reply) and not to the terminal. Since the player does
/// not see it, it may only read: a line that is not one of the commands quietCommandAllowed()
/// lets through is refused as `not allowed`. Beyond that the runner knows nothing of the
/// command:
///
/// - It is sent only when nothing else is under way: no operation, no pager, and MUME at a real
///   prompt that came after the last line sent to it, by anyone, with as many prompts seen as
///   lines sent and half a second gone; or three seconds of nothing sent and nothing said at
///   all. So no reply of the player's is still to come when the window opens. Until then the
///   request waits, at most eight seconds, and is refused as `not idle` after that.
/// - When the command has reached MUME the runner tells the parser (sig2_quietCommand BEGIN),
///   which from then on keeps the reply out of the terminal and hands its lines over
///   (QuietCapture; sig2_quietLine, sig2_quietEnded). The pager the parser hid is the only one
///   this operation owns, and is answered with Return until the reply is whole.
/// - Another line going to MUME meanwhile, the player's included, does not stop it: MUME
///   answers in order, so that line's reply follows this one's prompt. The parser is told
///   (FOREIGN), and closes the window at the next real prompt whatever came. Only while the
///   runner holds the hidden pager is the player's line a reason to stop: it would answer the
///   pager, which is quit first.
/// - Four seconds after the command reached MUME, or after the last pager answer, it fails as
///   `timeout`, and the parser is told at once (END): a reply that never comes cannot keep the
///   player's text out for longer.
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
    /// A quiet command's reply: from its reaching MUME, and from each pager answer.
    static constexpr int64_t QUIET_TIMEOUT_MS = 4000;
    /// How long a quiet command waits for MUME to be idle before it is refused.
    static constexpr int64_t QUIET_WAIT_MS = 8000;
    /// MUME is idle this long after the last line sent, once every line has had its prompt.
    static constexpr int64_t QUIET_SETTLE_MS = 500;
    /// And after this long with nothing sent and nothing said, whatever the count of prompts.
    static constexpr int64_t QUIET_IDLE_MS = 3000;
    static constexpr int MAX_QUIET_LINES = 2000;
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
        CHAR_TROPHIES,
        /// MMapper.Input.Quiet: any one line, its reply kept from the terminal.
        QUIET
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
        /// A quiet command: not submitted yet, MUME not being idle; another line reached MUME
        /// between its being submitted and its reaching MUME, so that reply would come first;
        /// the parser saw the prompt that ends its reply; more lines came than are kept.
        bool waiting = false;
        bool raced = false;
        bool ended = false;
        bool truncated = false;
        /// Lines of its reply that are not blank.
        int replyLines = 0;

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

    /// Whether MUME is idle, for the quiet command: the last chunk from MUME was a real prompt
    /// and nothing was sent since; lines sent that have not had their prompt yet (the answer to
    /// a pager is not one); when a line was last sent; when one was last sent or MUME last said
    /// anything.
    bool m_atPrompt = false;
    int m_owed = 0;
    int64_t m_lastSentAt = 0;
    int64_t m_lastActivityAt = 0;
    bool m_seenActivity = false;

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
    /// Runs the quiet command `payload` asks for (MMapper.Input.Quiet: `id`, `text`), or
    /// refuses it; publishes one MMapper.Input.Reply, when it is over. The payload's `id` must
    /// already be checked to be a non-empty string.
    void requestQuiet(const QJsonObject &payload, const Context &context);
    /// Cancels the running operation if it has this id; ignored otherwise.
    void cancel(const QString &id);
    /// The driving frontend is about to send `line` of its own: a pager the runner holds is
    /// answered with `q` first, and the running operation stops.
    void beforePlayerLine(const QString &line);
    /// Stops the running operation without sending anything more.
    void abort(const QString &reason);
    /// Fails the running step if its time is up, and sends a quiet command that was waiting
    /// for MUME to be idle. FrontendServer calls it four times a second while busy.
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
    void onQuietLine(const QString &line);
    NODISCARD bool quiet() const { return m_op.has_value() && m_op->kind == KindEnum::QUIET; }
    NODISCARD bool idle() const;
    void sendQuietWhenIdle();
    void maybeSetViewer();
    void noteViewerLine(const QString &line);
    void resetConnection();
};

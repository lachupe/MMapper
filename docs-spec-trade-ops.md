# Spec text for trade operations, the viewer and the Session.State additions

Written by package B (branch `trade-ops`) for the main session to put into
`docs/specs/mmapper_frontend_protocol_spec.md`. Section numbers refer to that file. The
readers' packages (`MMapper.Shop.*`, `MMapper.Guild.*`, `MMapper.Inn.Offer`,
`MMapper.Char.Skills`, `.Char.Trophies`) are package A's text.

## Into section 11 (Session state): two new fields

```text
MMapper.Session.State {
  "itemCommands":6,
  "trade":1,
  "viewer":"unknown",
  ...
}
```

- `trade` is the version of the trade operations MMapper runs for `MMapper.Trade.Request`
  (section 34); it is 1 today, with the actions of the table there marked v1.
- `viewer` is MUME's `change viewer` setting as far as MMapper knows it: `external` once
  MMapper has set it (below) or the player's own `change viewer external` went out, `simple` or
  `off` when the player's own line set that, and `unknown` otherwise. It goes back to `unknown`
  when MMapper connects to MUME again. What MUME answers to the setting is not read; the value
  says what was asked, not what MUME confirmed.

The package is also sent again when `viewer` changes.

## Into section 23 (Error handling): one new code

| `code` | Sent when |
|---|---|
| `invalid-trade` | an `MMapper.Trade.Request` or `.Cancel` has no object payload, or no non-empty `id` string (at most 128 characters) in it |

`unsupported` no longer covers `MMapper.Trade.Request` and `MMapper.Trade.Cancel`, which a
frontend may send.

## Amendment to section 14, "Inventory observation metadata and command outcomes" (:1871)

Replace "MMapper reports individual operations. The frontend owns sequencing, timeouts and
cancellation." with:

> MMapper reports individual operations. For item moves the frontend owns sequencing, timeouts
> and cancellation, as above: a drag stays the client's. Trade operations (section 34) are the
> exception: there MMapper sends the commands, answers MUME's pager, runs the steps one at a
> time and times them out, and the frontend only asks and shows the outcome.

## New in section 34, "Trade, guilds and viewed texts": requests and operations

### Requests

A frontend asks MMapper to hold a short conversation with MUME:

```text
MMapper.Trade.Request {"id":"<client id>","action":"<action>", ...arguments}
MMapper.Trade.Cancel  {"id":"<client id>"}
```

`id` is the frontend's own, a non-empty string of at most 128 characters; MMapper echoes it in
every `MMapper.Trade.Operation` of that request and never interprets it.

| action | arguments | what MMapper sends | v1 |
|---|---|---|---|
| `shop.list` | `filter?` (string: MUME's own `list` words) | `list` or `list <filter>` | yes |
| `shop.buy` | `number` (stock number, 1 or more), `count?` (1-20, default 1) | `buy <number>` or `buy <count> <number>` | yes |
| `shop.sell` | `items` (1-50 selector strings, e.g. `2.sword`) | `sell <selector>`, one step per item | yes |
| `guild.list` | | `prac` | yes |
| `guild.practise` | `name` (string), `times` (1-100, or `"limit"`) | `prac <name>`, once per step | yes |
| `inn.offer` | | `offer` | yes |
| `inn.rent` | | `rent` | yes |
| `inn.retire` | | `rent retire`, then `rent retire` again | yes |
| `char.trophies` | | `trop` | yes |
| `shop.pieces`, `shop.show`, `shop.value`, `shop.mend`, `shop.resize` | | | no: `refused` `unsupported` |

Every other action is refused as `unsupported`. Strings that are empty where a value is
needed, longer than 100 characters, or hold a control character (a newline included), and
numbers out of range, are refused as `invalid arguments`.

A request is refused, with one `MMapper.Trade.Operation` of status `refused`, when:

| `reason` | when |
|---|---|
| `unsupported` | the action is not one of v1's |
| `invalid arguments` | an argument is missing, of the wrong type or out of range |
| `observing` | the frontend is not the driving one (sent only to it) |
| `not connected` | MMapper is not connected to MUME |
| `not in the game` | `MMapper.Session.State.game` is not `playing` |
| `echo off` | MUME has taken over echoing (a password prompt) |
| `busy` | another operation is running: one at a time overall, since only one frontend drives |
| `pager open` | MUME's pager is showing a reply the runner does not own (the player's) |

A `Cancel` for an operation that is not running is ignored: its last status has gone out. An observing frontend's `Cancel` is answered with the `read-only` error.

### Operations

```text
MMapper.Trade.Operation {"id":"r7","action":"shop.sell","status":"running",
                         "step":2,"steps":3,"reason":"","text":[]}
```

Not replayed. Sent when the request is accepted (`running`, step 1), when each further step
goes out (`running`), and once at the end with one of the other statuses. Every field is always
present.

- `step` is the step in progress or, at the end, the last one tried (1-based; 0 when refused
  before any step). `steps` is how many steps the action has: 1 for most, the number of items
  for `shop.sell`, `times` for `guild.practise`, 2 for `inn.retire`, and 0 when not known ahead
  (`guild.practise` with `"limit"`).
- `status`:

| status | meaning |
|---|---|
| `running` | a step's command has been sent or is about to be |
| `done` | every step had the reply it wanted, or `guild.practise` reached its limit |
| `refused` | not started (the table above), or MUME refused the first step |
| `stopped` | ended early by something else: MUME refused a later step, a foreign line, the player's input, too many pages, the session released, the link lost, the character leaving the game, echo changing |
| `failed` | no recognised reply: none at the next real prompt, or none within 15 seconds |
| `cancelled` | the frontend's `MMapper.Trade.Cancel` |

- `reason` is empty for `running` and a plain `done`, else one of: the refusals above;
  `closed` (the shop's closed line), `no such thing` (MUME's miss), `refused` (a keeper's or
  teacher's refusal); `limit reached` (practise at the teacher's most, or no sessions left);
  `overlapping command`, `player input`, `too many pages`, `session released`, `disconnected`,
  `left the game`, `echo changed`; `no reply`, `not rented`, `timeout`; `cancelled`.
- `text` is what MUME said during the last step, as plain lines (at most 32 lines of at most
  1024 characters), for `refused` after a step, `stopped` and `failed`; empty otherwise. The
  readers' own packages carry the full replies.

What each step wants, judged at the next real prompt (a prompt that is not the pager) from the
readers' packages published in that window:

- `shop.list`: `MMapper.Shop.List`, or the shop's miss (an empty result, `done`).
- `shop.buy`, `shop.sell`: `MMapper.Shop.Deal` of kind buy or sell; its miss, closed or refused
  kinds end the operation.
- `guild.list`: `MMapper.Guild.Teacher`, or `MMapper.Char.Skills` away from a teacher.
- `guild.practise`: `MMapper.Guild.Practised` without a refusal. It stops after `times` steps,
  when used sessions reach the teacher's most, or when the teacher's table said no sessions are
  left; with `"limit"`, a refusal after the first step is the limit too (`done`).
- `inn.offer`: `MMapper.Inn.Offer`.
- `inn.rent`: the character leaving the game (`game` becomes `rented`); a prompt with lines but
  no rent is `failed` `not rented`.
- `inn.retire`: step 1 wants MUME's "please repeat that request" (`MMapper.Inn.Offer` with its
  retire flag); only then is step 2 sent, which wants the character leaving the game.
- `char.trophies`: `MMapper.Char.Trophies`.

A real prompt that comes before any line of the reply (MUME's prompt already on its way) is
not judged; the step waits for the next.

### How MMapper runs them

- Commands go through the same path as the driving frontend's `MMapper.Input.Command`, so
  mapper commands, `MMapper.Char.Command` and the readers see them. MMapper keeps a list of
  what it sent and matches it, in order, against the lines that reach MUME. Any other line
  going out while an operation runs stops it (`stopped`, `overlapping command`): the client
  must hold its own quiet refreshes (inventory, `info`) until the operation ends.
- The player's own input is never queued. If the player types while the runner's reply is at
  a pager, MMapper first answers the pager with `q`, stops the operation (`player input`) and
  then sends the player's line.
- Pager: in a step's reply, MMapper answers MUME's pager with Return once per new percentage
  (MUME shows the same percentage again after other output; that is not answered), or when a
  whole page of new lines came with the same percentage. After 20 pages it answers `q` and
  stops (`too many pages`). A pager in a reply the player asked for is the player's.
- Each step has 15 seconds from its command (and from each pager answer); then `failed`
  `timeout`, answering a pager it holds with `q`. Nothing is retried.
- A lost link, the session released, the character leaving the game (except the rent that
  `inn.rent` and `inn.retire` want) and a change of echo stop the operation; what has not gone
  out is not sent.
- `Cancel` stops the steps not yet sent and answers a pager the runner holds with `q`; a
  command already sent still gets MUME's reply, which the readers publish as usual.

## New in section 34: the viewer

MUME's viewer is for long texts (`help -v`, `view <message>` at a board, mail), not for
ordinary output taller than the screen, which goes through the pager. With `change viewer
external`, MUME sends each viewed text whole as GMCP `MUME.Client.View` (or MPI), which MMapper
opens in a window of its own.

```text
MMapper.View.Text {"title":"...","text":"..."}
```

- The viewer is claimed while the driving frontend subscribes to `MMapper.View`; an observing
  frontend's subscription never claims it, so it cannot take viewed texts away from a telnet
  player. The claim is worked out again on every `Core.Supports.*`, when the driving frontend
  changes and when a frontend disconnects.
- While claimed, every viewed text is published as `MMapper.View.Text` (not replayed) and
  MMapper opens no viewer window.
- On the first real prompt after the character enters the game, if the viewer is claimed,
  MMapper sends `change viewer external` once for that login, unless the player has set the
  viewer during this MUME connection. It waits for a prompt with no operation running and no
  pager open.
- MMapper watches the player's own lines going out (`cha[nge] vie[wer] <value>`, any
  abbreviation of `external`, `simple` or `off`), reports the value in
  `MMapper.Session.State.viewer`, and does not set it again in that MUME connection.

Not verified live: what MUME answers to `change viewer external`, and whether MUME ends its
pager line with a GA.

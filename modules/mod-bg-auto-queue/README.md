# mod-bg-auto-queue

AzerothCore module that periodically auto-queues eligible online players into
battlegrounds, grouped by level bracket, to help battlegrounds reach the
critical mass needed to pop. Players are opted in by default and can opt out
with an in-game command.

## How it works

On a configurable interval the module runs a **queue pass**: it gathers every
eligible online player, groups them by PvP level bracket (10-19, 20-29, …,
70-79, never mixed), selects one battleground per bracket, and solo-queues
everyone in that bracket into it. A warning is broadcast `WarningLeadTime`
seconds before each pass. Players choose whether to accept the queue popup;
declining carries no penalty and they are simply considered again next pass.
Declining or ignoring a module invite shows a chat hint about `.bgevents off`
(see `BgAutoQueue.DeclineHint.Enable`), and after
`BgAutoQueue.OptOutAfterDeclines` consecutive declined events the character is
automatically opted out with a notice — reversible with `.bgevents on`;
accepting an event invite resets the count. Manual queues never trigger any of
this.

Battleground selection per bracket:
- **Live-battleground reinforcement (priority).** If a normal battleground is
  already forming or in progress for that bracket with free slots, players are
  queued into it (not limited to the configured pool).
- **Otherwise, a random pick** from `BgAutoQueue.Pool`, preferring candidates
  whose minimum players per team the bracket can fill (a pool candidate that
  already has uninvited queuers is reinforced first); if none qualify, the
  smallest battleground is queued anyway to seed it.

Note that while a seeded below-minimum queue waits to fill, the core blocks
those players from joining the Random Battleground queue, and, unless
`JoinBGAndLFG.Enable = 1` is set (core default is `0`), also from the Dungeon
Finder, until the queue pops or they leave it. See the note in
`conf/mod-bg-auto-queue.conf.dist`.

📖 **For the full, player-facing explanation** — when events fire, what players
see, opt-out, battleground selection, the complete eligibility list, and the
commands — see **[docs/how-it-works.md](docs/how-it-works.md)**.

## Interactions with other systems

- **Cross-faction matcher (mod-cfbg).** `BgAutoQueue.CrossFaction = 1` judges
  viability on the total player count and assumes a cross-faction matcher such
  as [mod-cfbg](https://github.com/azerothcore/mod-cfbg) fills both teams. On a
  stock core the queue itself still requires the minimum in each faction, so
  mode 1 can queue faction-lopsided batches that never pop; set
  `CrossFaction = 0` there. Conversely, with mod-cfbg active keep
  `CrossFaction = 1` (mod-cfbg fills both teams from the total, so per-faction
  viability under-queues). The module logs a warning on either
  misconfiguration.
- **Toggling `CFBG.Enable` at runtime.** Players queued while mod-cfbg was in
  the other state sit in queue buckets the newly-active matcher never reads:
  they will not be invited until they leave the queue and re-join (or log
  out). This is a core/mod-cfbg limitation that affects manual queuers too;
  the module excludes such stale entries from its viability counts and logs a
  warning when it sees them. After flipping `CFBG.Enable` and reloading,
  expect players queued before the flip to need a re-queue (a worldserver
  restart avoids the situation entirely).
- **Deserter tracking noise.** If `Battleground.TrackDeserters.Enable` is enabled,
  players who decline auto-queue invites may appear in the deserter tracking
  table. This is informational only, not a player-facing penalty.
- **Queue announcer.** If `Battleground.QueueAnnouncer.Enable` is on in
  immediate (non-timed) mode, a mass pass emits one world announcement per
  player. Consider `Battleground.QueueAnnouncer.Timed` /
  `…PlayerOnly` to avoid spam.

## Operational notes

Things to be aware of when running this on a live server:

- **A pass queues everyone in one tick (burst).** All eligible players are
  queued during a single world update, and a `OnPlayerJoinBG` script hook fires
  **once per queued player**. On a high-population server this is a noticeable
  burst: any other module that listens on `OnPlayerJoinBG` (announcers, reward
  systems, statistics) will fire en masse, and the core BG queue announcer in
  immediate mode emits one line per player (see "Queue announcer" above —
  prefer its timed / player-only mode). Spreading the burst across multiple
  ticks is intentionally not implemented; size your `Interval` accordingly.
- **A large batch opens several instances.** The core creates at most one new
  battleground instance per queue update, so after a pass the module keeps
  nudging the queue every few seconds while a bracket still holds enough
  uninvited players for another instance. A 60-player bracket therefore fills
  multiple simultaneous instances within seconds instead of leaving the
  surplus queued until the next pass. Any leftover below one instance's worth
  stays queued and is served by normal core behavior (declines, leavers,
  manual joins) or by the next pass.
- **`.reload config` restarts the timer.** Every config reload resets the
  interval and re-applies `InitialDelay`. If you reload more frequently than
  `Interval`, the next automatic pass is pushed back each time and may never
  fire — use `.bgevents run` to trigger one on demand, or avoid reloading
  right before a pass is due.
- **Opt-out is stored per character via the core PlayerSettings system**
  (`source = "mod-bg-auto-queue"`; index 0: `0` = opted in, `1` = manual
  opt-out via `.bgevents off`, `2` = automatic opt-out after consecutive
  declines; index 1: the consecutive declined-invite counter). The core loads
  it on login, saves it on logout, and deletes it when the character is
  deleted — the module keeps no table of its own. **This requires the core
  config `EnablePlayerSettings = 1` for the opt-out (and the counter) to
  persist across logins.** With it `0` (the core default), `.bgevents off`
  still works but only for the current session, and a warning is logged at
  startup.

## Installation

1. Clone this folder into `modules/mod-bg-auto-queue/` of your AzerothCore source.
2. Re-run CMake and rebuild the worldserver.
3. Copy `mod-bg-auto-queue.conf.dist` to `mod-bg-auto-queue.conf` in your
   worldserver's configuration directory and adjust as needed.
4. For per-character opt-out to persist across logins, set the core config
   `EnablePlayerSettings = 1` (see the dependency note in the conf). The module
   creates no database tables of its own.

## Configuration

All options are documented in `conf/mod-bg-auto-queue.conf.dist`:

- `BgAutoQueue.Enable` — enable the automatic periodic pass (calling `.bgevents run` works even when disabled).
- `BgAutoQueue.Level.Min` / `BgAutoQueue.Level.Max` — eligible level range.
- `BgAutoQueue.Pool` — CSV of `battleground_template` IDs to pick from.
- `BgAutoQueue.Interval` — minutes between passes (`0` disables the schedule).
- `BgAutoQueue.InitialDelay` — seconds before the first pass after startup/reload.
- `BgAutoQueue.WarningLeadTime` — seconds before a pass to broadcast the warning.
- `BgAutoQueue.BroadcastMessage` — the warning text (empty disables it).
- `BgAutoQueue.CrossFaction` — how the available count is judged against a BG's minimum players per team; `1` requires a cross-faction matcher such as mod-cfbg (see "Interactions with other systems").
- `BgAutoQueue.SkipGameMasters` — skip GMs in the warning and the queueing.
- `BgAutoQueue.SkipAFK` — skip players flagged AFK (on by default; note the client auto-flags idle players).
- `BgAutoQueue.SkipAuras` — CSV of aura IDs whose carriers are skipped for the pass.
- `BgAutoQueue.DeclineHint.Enable` — chat hint about `.bgevents off` after a declined/expired module invite (the auto-opt-out notice below is always sent).
- `BgAutoQueue.OptOutAfterDeclines` — consecutive declined module events before the character is automatically opted out (`0` disables the auto-opt-out).

## Commands

- `.bgevents on` — opt the current character back into battleground events (also resets the consecutive-decline count).
- `.bgevents off` — opt the current character out (future passes only; does not dequeue an existing queue).
- `.bgevents` — *(no argument)* show the opt-in state and the time until the next scheduled pass.
- `.bgevents run` — *(GM, console-capable)* run a queue pass immediately, even when the automatic schedule is disabled. Does not reset the periodic timer.

Note: while a character holds the auto-created queue entry, the client
silently rejects a manual re-join of that same battleground — solo, and for
the whole party on "Join as Group" if any member holds the entry. Players who
want to queue as a group right after a pass must first leave the automatic
queue (via the queue icon next to the minimap) or wait for the invite.

# Battleground Events — How It Works

Every so often the server runs a **Battleground event**. When an event fires,
everyone who's eligible is automatically put into the queue for a Battleground,
all at the same time. The whole point is to gather enough people at once so the
Battleground actually pops and you get a real, full match instead of waiting
forever in an empty queue.

You don't have to do anything to take part — **you're opted in by default.**

## When events happen

Events run on a **timer**: roughly every so many minutes, the server starts one.
That interval is a server setting — it may be short on a test realm so events
come quickly, and longer on the live realm. You can always check the countdown
to the next one with **`.bgevents`**.

Game Masters can also start an event **on demand** with **`.bgevents run`**. That
fires an event immediately, on top of the normal schedule; it does **not** reset
the countdown, so the next *scheduled* event still happens at its usual time.

## What you'll see

1. **A heads-up first.** Shortly before each event (about a minute before), you
   get a chat message warning you it's about to start.
2. **The queue pop-up appears.** When the event fires, the normal Battleground
   "enter / leave" window pops up on your screen, just like when you queue
   yourself.
3. **You decide.** Click **Enter Battle** to join the match, or just ignore /
   decline the pop-up if you don't feel like it this time.
4. **If you decline or ignore the pop-up**, a chat message reminds you how to
   turn events off for good (`.bgevents off`), in case you'd rather not be
   asked at all.

That's it. Accepting drops you into the Battleground like any normal queue.

## Declining is totally fine

If you ignore or decline the pop-up, there's **no penalty** — no Deserter
debuff, no cooldown — and you get another chance at the next event. The
Deserter debuff only ever applies if you *enter* a Battleground and *then*
leave it early, which has nothing to do with this.

One thing to know: if you decline **several events in a row** (a server
setting, normally 3), the server assumes you're not interested and **stops
auto-inviting you**. A chat message tells you when that happens, and
**`.bgevents on`** brings events back whenever you want. Accepting any event
invite resets the count.

## Which Battleground you get

- Normally the event picks one **at random** from a pool — currently **Warsong
  Gulch, Arathi Basin, and Eye of the Storm** — choosing only from those
  available for your level bracket. (More Battlegrounds may be added to the pool
  later.)
- **One exception:** if a Battleground for your bracket is **already running**
  and has free space, the event sends everyone to **that** match instead of
  picking a random one — so you reinforce the live game rather than splitting
  players across two.

## Level brackets

Only characters within the eligible level range take part — by default that's
**level 10 to 79** (max-level characters are not auto-queued). Players are
grouped into the normal PvP level brackets (10–19, 20–29, … 70–79) and are
**never mixed across brackets**, so you play with people near your level.

You're also **queued solo**: the event queues each person individually, not as a
premade party with your group.

## Opting out (and back in)

If you'd rather never be auto-queued, you can turn it off for your character:

- **`.bgevents off`** — stop being auto-queued (you'll no longer get pop-ups).
- **`.bgevents on`** — opt back in.
- **`.bgevents`** — check whether you're opted in, and see how long until the
  next event.

The server also opts you out **automatically** after several declined events
in a row (see "Declining is totally fine" above) — `.bgevents on` undoes that
too.

Opting out only affects **future** events; it won't remove you from a queue
you're already in.

## When you *won't* be auto-queued

You're skipped for an event (this event only) if you're:

- **opted out** — with `.bgevents off`, or automatically after several
  declined events in a row,
- **outside the eligible level range** (by default below 10 or at max level),
- **in a dungeon or raid**,
- **already in a Battleground**,
- **already in a Battleground or Arena queue**,
- under a **Deserter** penalty,
- **using the Dungeon Finder (LFG)**,
- a **Death Knight still in the Ebon Hold starting zone**,
- a **Game Master** (skipped by default; this can be turned off),
- **flagged AFK** (skipped by default; this can be turned off — note the game
  flags you AFK automatically after a few minutes of inactivity, so if you were
  idle you may get neither the warning nor the pop-up),
- carrying one of the **configured skip auras** (a server setting).

Once whatever's blocking you clears, you'll be considered again at the next
event.

## You can still join manually

Don't want to wait for an event? Open the Battleground window (press **H**) and
queue yourself as usual. Events are just a convenience on top of that.

**One catch:** while you hold the queue spot an event gave you, the game
silently ignores a manual re-join of that **same** Battleground — nothing
happens when you click. The same applies to **Join as Group**: it silently
fails for the whole party if any member holds the event's queue spot. To queue
for that Battleground with friends right after an event, first **leave the
event queue** (click the battlefields icon next to the minimap and leave) —
then queue as a group normally. You can also wait for the invite and decline
it, but that counts toward the automatic opt-out described above; leaving the
queue before the invite pops doesn't. Queueing for a *different* Battleground
is unaffected (you can be in two queues at once).

## Command reference

| Command | Who | What it does |
|---|---|---|
| `.bgevents` | Everyone | Show your opt-in status and time to the next event |
| `.bgevents off` | Everyone | Stop being auto-queued |
| `.bgevents on` | Everyone | Start being auto-queued again |
| `.bgevents run` | Game Master | Trigger an event immediately (does not reset the timer) |

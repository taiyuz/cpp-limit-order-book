# Design notes

This is a single-instrument, single-threaded matching engine. The goal is a
layout an interviewer can audit in ten minutes: integer ticks, intrusive FIFO
levels, O(1) id lookup, no allocations on the steady-state hot path, no
virtuals, no exceptions there either.

## Layout

```
MatchingEngine
├── NodePool          slabs of alignas(64) OrderNode, free-list reuse
├── OrderBook
│     ├── bids: std::map<Price, PriceLevel, greater<>>   best = begin()
│     └── asks: std::map<Price, PriceLevel, less<>>      best = begin()
│           └── PriceLevel: intrusive doubly-linked FIFO (head = oldest)
└── unordered_map<OrderId, OrderNode*>                   cancel / modify / replace
```

`OrderNode` is exactly 64 bytes: `prev`, `next`, `level*`, `id`, `price`,
`remaining`, `seq`, `side`. One cache line per resting order. The `level*`
back-pointer is what makes cancel O(1) unlink instead of a scan.

### Why not `std::map<Price, std::deque<Order>>`

| | map-of-deques | this |
|---|---|---|
| Cancel | O(n) at the level, or store deque iterators that invalidate | O(1) pointer unlink |
| Allocation | deque chunk new, plus Order copies | slab pop / push, no per-order `new` |
| Match walk | hop between deque blocks | hop `next` pointers, node is one line |
| PriceLevel* stability | N/A | `std::map` nodes are stable, so back-pointers stay valid |
| Code | shorter | more moving parts, explicit |

A densified tick ladder (`vector<PriceLevel>` indexed by `price - min_tick`)
beats the map when the live range is tight and dense (equities at 1¢, FX). It
loses when the range is wide (crypto, bonds) because you either waste RAM or
rebucket. A red-black map of *live* prices is the honest default: P is tens to
low thousands, `log P` is 10–15 comparisons, and we only pay it when a level is
created or destroyed.

`std::pmr` would also work. A typed free-list on 64-byte nodes is simpler, has
no upstream/downstream allocator dance, and is trivial to reason about under
ASan.

## Complexity

Let P = number of distinct live prices, N = resting orders, F = fills produced
by one incoming order. Hash ops are amortized expected O(1).

| Operation | Time | Notes |
|---|---|---|
| Limit rest (no match) | O(log P) | map `try_emplace` + O(1) pool + O(1) hash insert |
| Limit / market match | O(F + L log P) | each fill is O(1); L empty levels erased |
| Cancel | O(1) typical | O(log P) if that order was the last at its price |
| Modify qty down, same px | O(1) | stays in FIFO, keeps time priority |
| Modify qty up or price | O(log P + match) | pulled, loses time priority, may re-cross |
| Replace | O(log P + match) | cancel + new GTC limit; always new id / time priority |
| BBO / `top()` | O(1) | `map::begin()` |
| Quantity at a price | O(log P) | map find |
| `snapshot()` / `dump()` | O(P + N) | allocates; debug only, not the hot path |

Incoming orders match at the **maker** price (price-time). Buy limits cross
asks with `ask <= limit`; sell limits cross bids with `bid >= limit`. The book
never rests a crossing order, so it never locked (bid >= ask).

## Integer ticks

Prices and quantities are `int64_t`. Tick size and lot size belong at the
gateway: `price_ticks = llround(px / tick)`. Floats on the hot path invite
`0.1 + 0.2` bugs, broken inequality, and non-reproducible fills. Exchanges do
not match on IEEE-754.

Zero and negative limit prices / qty are rejected with `std::expected`. Market
orders ignore price. There is no overflow check on `id` / `seq`; both are
monotonic `uint64`.

## Hot-path constraints

- No virtual dispatch. `MatchingEngine` is a concrete type.
- No exceptions on submit/cancel/modify/replace. Errors are `std::expected<FillReport, Error>`.
  (`NodePool::grow` can throw `std::bad_alloc`; that is the setup path, not a fill.)
- No per-order `new`/`delete`. Steady-state construct/destroy is a pointer bump
  on the free list. First touch of a slab is the only malloc.
- `trades_` is a reused `vector` with reserved capacity. Callers must copy
  `last_trades()` before the next mutating call.

## What lock-free would look like

Do **not** make the tree lock-free. Concurrent `map` mutation plus FIFO unlinks
plus a hash index is a research project, and the loser is tail latency.

The production shape:

1. **Shard by instrument.** One `MatchingEngine` per symbol, pinned to a core.
   Ingress is a well-known ring (LMAX Disruptor / Folly MPMC with a single
   consumer). Producers never touch the book.
2. **Sequence the matching thread.** The matching core is the only writer. That
   recovers the single-threaded design above, which is the point of this repo.
3. **Publish BBO with a seqlock.** Readers (market-data, risk) retry on an odd
   sequence. No reader ever takes a mutex on the book. Depth-of-book snapshots
   are epoch-copied, not walked live.
4. **If you truly need one book, many cores:** an atomic intrusive list per
   level plus a lock-free skip list of prices (or a pre-allocated tick array)
   plus hazard pointers / QSBR for retired nodes. Cancel still wants the id
   map, which becomes a concurrent hashmap. The fill path now has ABA and
   memory-order homework on every hop. That is how you miss a fill.

For an interview: sharding + single-writer + seqlock publish is the answer
that ships. Lock-free matching is the answer that gets a follow-up.

## Multi-instrument sketch

```
Gateway ──► shard(instrument_id) ──► engine[i].submit(...)
                                  └──► md_bus.publish(engine[i].top())
```

- `std::vector<unique_ptr<MatchingEngine>> engines;` indexed by a dense
  instrument id assigned at session start. Sparse ids go through a hash.
- Each engine has its own pool, so a busy name does not fragment a quiet one.
- A cancel must land on the same shard as the original submit (sticky routing
  by id, or encode the shard in the high bits of `OrderId`).
- Cross-instrument (spreads, calendar) is a **different** matcher sitting on
  top, holding legs with a two-phase "try fill / commit" against two engines.
  Not in this repo.

## Matching rules (this implementation)

| Type | Rest remainder? | Notes |
|---|---|---|
| Limit | yes (GTC) | |
| Market | no | leftover is cancelled, reported in `remaining` |
| Cancel | — | unknown id → `Error::NotFound` |
| Modify qty down, same price | yes | keeps queue position |
| Modify qty up or price | maybe | new time priority; aggressive price will take |
| Replace | maybe | cancel then new GTC limit, same side; new id; always new time priority |

No IOC/FOK flags, no hidden/iceberg, no auctions, no self-trade prevention, no
STP, no lots/tick validation beyond `> 0`, no persistence, no recovery log.

Replace is not modify. `modify` keeps the order id; qty-down at the same price
keeps FIFO place. `replace` always allocates a new id, always goes to the back
of the (new) level, and never flips side. Bad qty/price reject without pulling
the original. Unknown id is `NotFound` and inserts nothing.

## Debug snapshot

`MatchingEngine::snapshot()` copies live levels (best first) and the FIFO at
each level. `dump(std::ostream&)` prints that for tests and gdb. Neither is a
recovery image: there is no restore, WAL, or crash replay. Keep them off the
matching hot path; they allocate.

## Testing stance

Correctness is in `tests/test_engine.cpp` and `tests/test_replace.cpp` (FIFO,
price-time, partials, market walks, cancel-in-the-middle, modify priority,
replace new-id / FIFO loss / aggressive remainder / missing id, book never
crosses, empty-book market, unique ids, no STP, debug snapshot). CI runs that
suite under ASan+UBSan. Latency is a separate harness so sanitizers do not
pollute numbers.

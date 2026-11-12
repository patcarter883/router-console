# Persistent traffic graphs

WinBox keeps a per-interface traffic history and draws it over minutes or hours.
This milestone gives the client the same shape: the 1 Hz sampler runs for as long
as the client is connected, the samples land in a bounded in-memory store, and the
interface window can ask for four windows of it.

Everything here is in memory for the life of the process. **History starts empty
every time the client starts**, and it is kept across a disconnect and reconnect
within one run (the sample clock is per process, so timestamps stay monotonic).
There is no disk database and no export; a project file is a later concern.

## Where the code is

| File | Role |
| --- | --- |
| `src/models/TrafficHistory.{h,cpp}` | The store: retention, decimation, per-window traces. No Qt objects, no I/O. |
| `src/models/InterfacesModel.cpp` | Feeds the store from every `network.device.status` sample, exposes `history(ifName, windowMs)` to QML. |
| `src/qml/TrafficGraph.qml` | Time-based line graph: one value per grid slot, gaps break the line, no chart module. |
| `src/qml/InterfaceWindow.qml` | Range selector (1m / 10m / 1h / 24h) and the store-backed graph. |

## Sampling is owned by the connection, not by the window

`InterfacesModel::start()` is called by `AppEngine` when the login completes, and
`stop()` when the client disconnects. The 1 Hz timer lives there, so it keeps
running with no graph window open — a window opened ten minutes after connect
draws those ten minutes. The old rolling 120-point arrays are still filled for
the row roles, but they are no longer what the graph reads.

Every interface whose device is present contributes a sample per tick
(`rx bytes/s`, `tx bytes/s`, one timestamp). An interface whose device is absent
contributes nothing, which the graph shows as a gap rather than as zero traffic.

## Sampling has to survive the session

A day-long history is only as good as the sampler's reliability, and a long run on
the lab VM turned up a real gap. rpcd sessions expire (300 s on this VM) and
`UbusClient` renews one by logging in again, parking the calls that arrive
mid-renewal. Two defects made that renewal drop work: the parking space held a
single call, and the renewal path cleared the queue it had just been added to.
Either way the dropped request's handler never ran, its caller kept its in-flight
flag set, and the 1 Hz loop stopped sampling for good — silently, with the status
bar still reading "connected".

Measured before the fix, 620 s run:

    SKIPPED: timed out after 570 samples (state: connected to 127.0.0.1)

570 s is two session lifetimes, i.e. the second renewal. Every waiting call is now
parked in a queue, a renewal started by the queue leaves it intact, and a failed
login fails all parked calls with the login error instead of dropping them. The
same 620 s run after the fix: `samples=620 recent=600 coarse=3 retainedMs=620074`.

## Two tiers, bounded memory

Keeping 24 h at 1 Hz means 86 400 samples per interface per direction. The store
holds two tiers instead:

| Tier | Resolution | Span | Cap |
| --- | --- | --- | --- |
| recent | 1 s | last 10 min | 601 samples |
| coarse | 10 s weighted buckets | older, back to 24 h | 8 640 buckets |

When a sample falls out of the recent tier it is folded into the 10 s bucket it
belongs to, weighted by the number of samples the bucket already carries, so the
bucket keeps the mean of its samples rather than of its bucket count. A per
interface series costs about 290 KB at full retention (measured: 601 samples ×
24 B + 8 580 buckets × 32 B).

## Bucketing per range

A query re-buckets both tiers onto an absolute grid (`floor(t / bucketMs)`), so a
slot boundary does not move as new samples arrive — the trace grows on the right
instead of sliding under the pointer. The width comes from a ladder picked so the
point count stays at or under 1500, and never finer than 10 s once the coarse tier
is in play:

| Range | bucket | points |
| --- | --- | --- |
| 1 min | 1 s | 61 |
| 10 min | 1 s | 601 |
| 1 h | 10 s | 361 |
| 24 h | 1 min | 1 441 |

So what bucketing keeps the UI smooth? Measured on the lab VM after a 15 minute
run, 627 paints per window (`rc-paint` lines below):

| view | points | trace build | Canvas `onPaint` |
| --- | --- | --- | --- |
| 10 min, 1 s | 601 | 30 µs (unit test) | 0.60 ms |
| 24 h, 1 min | 1 441 | 21 µs (unit test) | 0.64 ms |

The paint callback is dominated by fixed work (grid, axis labels, path setup):
2.4× the points cost 7 % more. The binding constraint is therefore legibility,
not CPU — a 1 441 slot trace over a 1 100 px plot is already 0.76 px per slot, so
the 1 500 point cap is where 24 h at 1 minute resolution lands and anything finer
would draw sub-pixel detail. The reason the recent tier is decimated at all is the
other resource: 24 h at 1 Hz would be 86 400 samples per interface per direction
(2 MB), against 290 KB for the two tiers.

## Range selector

The interface window carries the four ranges as buttons; switching re-queries the
store, which is a pure function of the retained history, so the same data is drawn
at another zoom with no re-sampling. The footer states what is being shown:
`10 s buckets · 361 points · sampled 6 m 12 s of 1h · history keeps 24 h`.

## Measuring the paint cost

`TrafficGraph.qml` accumulates the duration of its `onPaint` callback and
`InterfaceWindow` hands the average to the model on each sample. Set
`RC_PAINT_STATS=1` and the model prints one line per report:

    rc-paint bucketMs=1000 points=601 ms=0.62 avg=0.58 paints=1 reports=509

The paint callback runs on the render thread, so the value is reported from a
GUI-thread call rather than from inside the callback itself.

## Tests

- `m16-traffic-history` (no router): grid placement, the 10 min/24 h retention
  caps, coarse-bucket means, tier merge by sample count, gaps, the width ladder,
  and the cost of a full day of samples.
- `m16-traffic-history-live` (lab VM, `RC_TEST_HOST` / `RC_TEST_SSH_PORT`): ssh
  traffic through the VM for 30 s **with no window open**, then asserts the recent
  tier holds the run's samples, the four ranges report 1 s / 1 s / 10 s / 5 min
  buckets, the 1 min view carries a visible peak, and the same minute asked for at
  1 s and at 10 s agrees on its mean within 2 %.
- `scripts/m16-shot.sh` produces `docs/evidence/m16-graphs.png` (24 h) and
  `docs/evidence/m16-graphs-10m.png` (10 min) from two clients sampling the same
  VM run.

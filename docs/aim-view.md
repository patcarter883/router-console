# Cellular map and aiming view (m17)

The modemaim plugin gets its operational half: a map of the ACMA sites the
router knows about, and a rose for aiming a directional antenna by hand.

    plugins/modemaim-map/    Cellular Sites    map, site detail, band filter
    plugins/modemaim-aim/    Cellular Aiming   bearing rose, peak hold, marks

Both are plain plugin windows: two `plugin.json` manifests and two QML files
that bind the same rpcd surface as the m6 summary window. They share
`Aim::Model` (`src/models/AimModel.{h,cpp}`), which holds the site list, the
peak-hold store and the operator's marks.

## What there is no rotator for

The owner's correction governs the design: **"the timed sweep is effectively
impossible if it's being rotated by hand"**. There is no heading reconstruction
here. Nothing infers where the antenna points from a sequence of samples, and
`modemaim bearing` (a stored number written by the LuCI page) is not used as a
heading. The rose shows where the sites are; the operator turns the mast and
types the bearing they believe they are on.

## Data, measured on the VM

| source | what it gives |
| --- | --- |
| `modemaim signal` (500 ms) | `ts,rssi,rsrp,rsrq,sinr` — one MBIM query (~0.07 s), the fast lane the peak hold runs on |
| `modemaim status` (3 s) | `state,pstate,plmn,operator,tech,mapped{site,conf,source}`, the same metrics, `serving{pci,earfcn,rsrp,rsrq,ta,ta_us,eci}`, `neighbors[]` |
| `modemaim origin` (30 s) | `lat,lon,confirmed` — the router's own position, the centre of both views |
| `GET /luci-static/resources/modemaim/towers.json` (60 s) | `data_date,bbox,towers[{site,lat,lon,band,mhz,bw,eirp,h,azs,hbw,carrier}]` |

The extract is one row per site **per licensed band**: 3,810 rows, 328 distinct
sites inside the bench extract's bounding box. `Aim::Model` aggregates them to
one point per site, keeping the band list, the frequencies, the licensed panel
azimuths (`azs`) and the half beamwidth (`hbw`).

The serving cell **is** on the surface, so it is correlated rather than guessed:
when `status.serving.eci` is present and `status.mapped.site` names a site in the
extract, the aim view marks that site on the rose and prints its bearing and
distance. If the cell is unmapped the view says so; it never invents a site.

## Map (`Cellular Sites`)

Local equirectangular grid centred on the origin: x = Δlon·111.195·cos(lat0),
y = −Δlat·111.195, in kilometres, so ring radii are true distances and a site's
angle on the canvas is its bearing. No tiles, no map API, no network beyond the
router. Distance rings step 0.5/1/2/5/10/20/25/50… km by zoom, the canvas
scrolls to zoom and drags to pan, sites are coloured by band, the serving site
carries a green ring, and clicking a site (or a row in the nearest-sites list)
fills the detail panel: site id, carrier, bands and MHz, licensed panel
azimuths, height, EIRP, bearing and distance from the router. It opens on the
nearest site.

## Aim view (`Cellular Aiming`)

Bearing rose, N up, a tick every 15°, labels every 45°, rings every quarter of
the selected range (5/10/25/50 km or the whole extract). Sites are plotted at
their bearing, radius by distance; sites beyond the range sit faded on the rim.

The peak hold is a running maximum per metric with the timestamp it was
measured — RSRP, RSRQ and SINR all read better higher, RSRQ included (it is a
negative dB figure), so each peak is a max. Live and peak are shown side by
side; `Reset peaks` clears them.

`Mark` (or Enter in the bearing field) snapshots the bearing the operator typed,
their free-text note, the current reading **and** the peaks into the candidate
list; each mark is drawn as a tick on the rose at that bearing, and the note is
kept with the numbers. Marks and peaks live in memory for the session, like the
m16 history: closing the window (or the client) ends them. There is no disk
persistence and none is claimed.

## Lab fixture

The VM has no MBIM modem, so `scripts/vm-aim-fixtures.sh` installs a stand-in
`/usr/bin/mbimcli` that answers the four queries the rpcd plugin makes with
metrics that swing over a minute, plus the curated `enb-acma.csv` row that maps
the stand-in's serving cell onto a real site in the extract (10021260, 1.35 km
from the bench origin). Everything above `mbimcli` — the rpcd script, the JSON
shapes, the extract, the eNB lookup — is the app's own code. The RF numbers are
a fixture and are labelled as such.

## Tests

| test | what it proves |
| --- | --- |
| `m17-aim-model` | bearing/distance against published pairs (0.1° of latitude, Sydney→Melbourne 713 km at 230°), site aggregation from per-band rows (including a row with no coordinates), peak-hold over synthetic series (including polls missing a metric and polls with no metrics at all), mark round-trip and immutability of an existing mark, ingest dedupe and failure handling |
| `m17-aim-live` | against the VM: 32 s on the `signal` lane, ≥ 200 sites with valid positions and bearings, nearest site, peaks equal to the best figures seen in the run, live RSRP present, serving cell mapped to an ACMA site, and a mark added through the model API that reads back with its bearing, time, live and peak figures |
| `m17-aim-ui` | both plugin windows load on a headless X display with no QML errors and produce the evidence screenshots; skips (exit 3) when the VM is not reachable |

Evidence: `docs/evidence/m17-map.png`, `docs/evidence/m17-aimview.png`
(`scripts/m17-shot.sh`; it types a bearing and presses Enter, so the shot holds
a real mark).

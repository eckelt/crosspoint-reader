# Deskleaf calendar (fork feature, X4 Pro)

Shows the calendar of the Deskleaf bridge (macOS, repository
`eckelt/deskleaf-display`) on the Xteink X4 Pro. Built only in the `x4pro`
profiles (`-DDESKLEAF_CALENDAR=1`).

## What it does

- **Sleep screen "Calendar"** (Settings → Display → Sleep screen): today's
  timeline is drawn when the reader sleeps. An RTC timer wakes the device
  2 minutes later and then on the Deskleaf schedule (workdays 07:00–19:00
  every 15 min, otherwise at 07:00, noon on weekends and 00:05). Each timer
  wake syncs over BLE and redraws only when today's view changed, then sleeps
  again without starting the UI or the frontlight. Any button press during a
  timer wake cancels it and boots normally.
- **Home → Calendar**: day, week and month views. Left/Right or swipes page,
  Confirm switches the view. A header tap does too when today's day, week
  or month is shown, otherwise it returns to today in that view. A long
  press on Confirm or a long touch syncs, a tap on a day in week/month opens
  it. A tap on an event
  in the day view shows its details; Back, Confirm or a tap returns.
- The month view shows the calendar week at the bottom of each Monday cell.
- Weekends show Saturday and Sunday side by side on a shared hour axis, on
  the sleep screen and in the day view (paging goes Friday, weekend, Monday).
- Event boxes: title on the left; on the right the place icon (Teams, Meet,
  Zoom, Webex, NE, LinkedIn, phone or an on-site pin), series and attachment,
  20x20 from `scripts/deskleaf_icons`. Then time · organizer +attendees,
  location and notes; the Obsidian icon sits bottom right.
- A free day (and a completely free weekend) shows the Deskleaf free-day
  motif from deskleaf-display. `scripts/deskleaf_freeday_art.py` generates
  `src/deskleaf/FreeDayArtData.cpp` from it and widens the sides so the sky
  fades into the paper on the wider screen.
- Multi-day events show on every day they cover. Timed ones keep their times
  on the first day ("ab 09:00") and the last day ("bis 17:00"); fully covered
  days in between and all-day events show as day bars. Not shown on
  cover-grid home themes.
- The app shows the stored calendar at once. Data older than an hour syncs on
  entry. The sync runs on a worker task (`deskleaf::bgsync`), so paging and
  details stay usable. A screen change or sleep stops it first, and the BLE
  page turner pauses meanwhile, because both use the same BLE stack.

## Bridge protocol

The device keeps a window of 40 weeks (13 back, the current one, 26 ahead)
as one file per week, and asks with
`WREQ:<set hash>:<battery mV>:<first Monday>:<weeks>`. The set hash is FNV-1a
over `YYYY-MM-DD:HASH8\n` per week of the window; a week's hash is FNV-1a
over exactly the JSON bytes the bridge sent for it, 0 when it is empty. The
bridge (deskleaf-display, `swift/Sources/DeskleafCore/WeekSync.swift`) answers
`TIME`, `TZ` and either `SAME` or `LEN[F]:<n>` with frames
`W:<Monday>:<n>\n<JSON array>\n` for the weeks that changed (`n` = 0: empty
now). It remembers its last two deliveries: when the device's set hash matches
one of them, only the weeks changed since are sent; otherwise all of them. An event is listed in
every week it covers, and the device merges the weeks it loads without
duplicates. Contract vectors on both sides: `test/deskleaf_calendar`,
`WeekSyncTests.swift`.

The ESP32-C3 Deskleaf keeps its `REQ` payload. Until the first week sync the
X4 Pro still shows the `calendar.json` of earlier builds. Without a set clock the X4 Pro
sends one `REQ` first to get `TIME`, then the `WREQ`. While no timezone was
ever chosen in Settings (the clock keeps UTC), the first sync adopts the
bridge's `TZ` and saves it; a zone picked by the user always wins. The
Deskleaf firmware OTA and battery-history messages are not used.

Files in `/.crosspoint/deskleaf/` on the SD card: `weeks/<Monday>.json`,
`weeks.idx` (Monday and hash per stored week) and `state.json`. The calendar
app loads only the weeks its view shows; the sleep screen loads today's week.

## Updates

The fork's CI (`.github/workflows/deskleaf-x4pro.yml`) publishes a GitHub
release for every push to `deskleaf-calendar`: tag `2.0.<run number>`, asset
`crosspoint-<tag>-x4pro.bin`. Builds with `DESKLEAF_CALENDAR` check this fork's
latest release in Settings → firmware update over Wi-Fi, not the upstream
releases. Install the first build with OTA support over USB or from the SD
card; older Deskleaf builds still ask upstream.

## Verify

- Host tests: `ARDUINOJSON_SRC=<ArduinoJson>/src ./test/deskleaf_calendar/run.sh`
- Firmware: `pio run -e x4pro`, or the `Deskleaf X4 Pro firmware` workflow
  on `deskleaf-*` branches (artifact `firmware-x4pro-deskleaf`).
- On device: bridge running, open Home → Calendar, long-press Confirm; serial
  log tag `DSK` shows `REQ sent` and `Sync: ...`. Select the Calendar sleep
  screen, sleep, and watch for `Calendar timer wake` about 2 minutes later.

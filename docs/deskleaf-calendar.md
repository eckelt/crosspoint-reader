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
  in the day view opens a popup card over the faded day (when, calendar,
  title, icon chips, place, organizer, notes preview; as tall as its
  content). A tap on the card or Confirm opens the detail page with its own
  header (calendar · date, title, time); a tap beside it or Back closes it.
- Notes: the detail view renders a Markdown subset (headings, bullets, tasks,
  rules; HTML comments and link markup removed, `parseNotes`); event boxes
  show the same notes as plain lines. Long notes continue on further pages:
  Left/Right or swipes page, "weiter »" marks that more follows.
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

The X4 Pro notes the events only it sees (sleep screen redrawn, bridge not
found, a new firmware at the first sync after an update) in `activity.hist`
and sends them first as `XACT:<epoch ms>:<type>;...` lines (types as the
Deskleaf's ACT: 4 refresh, 5 firmware, 6 bridge not found). Syncs that never
reach the bridge keep their battery reading in
`battery.hist`; the next sync sends them first as
`XHIS:<epoch>:<mV>;...` lines (each one BLE write), and the bridge files them
under the X4 Pro with their own time. A bridge without XHIS refuses the line;
the readings then stay on the card and the sync goes on.

The ESP32-C3 Deskleaf keeps its `REQ` payload. Until the first week sync the
X4 Pro still shows the `calendar.json` of earlier builds. Without a set clock the X4 Pro
sends one `REQ` first to get `TIME`, then the `WREQ`. While no timezone was
ever chosen in Settings (the clock keeps UTC), the first sync adopts the
bridge's `TZ` and saves it; a zone picked by the user always wins. The
Deskleaf firmware OTA and battery-history messages are not used.

Files in `/.crosspoint/deskleaf/` on the SD card: `weeks/<Monday>.json`,
`weeks.idx` (Monday and hash per stored week) and `state.json`. The calendar
app loads only the weeks its view shows; the sleep screen loads today's week.
The to-do list adds `todos.txt` (the last list from the worker),
`todo-queue.txt` (taps not yet sent) and `todo-attempt.txt` (time of the last
Wi-Fi visit for it); `spick-rescued` marks the one-time move of old notes;
`hirn/` and `hirn-stamp.txt` hold the Brain's notes (see below).

## Spick and Heute

Two home tiles use the worker over Wi-Fi (`deskleaf-feed.txt`, see above):

- **Spick** shows Markdown notes from the display inbox
  (`GET /display/inbox`). Opening it fetches in the background; Confirm or a
  tap on the title row fetches again. Notes are saved to `/Spick/<inbox id>` and checked
  off (`/done`) only once they are on the card. Notes that firmware before
  2.0.69 saved to `/Inbox` move to `/Spick` once.
- **Checklisten** receive inbox entries of kind `list` (`<Name>.list`, one
  item per line): the items are appended to `/Checklisten/<Name>.txt` as open
  `- [ ]` lines; items already on the list keep their tick.
- **Heute** shows the daily to-do list (`GET /display/todos`). A tap ticks an
  item or opens it again; taps are stored at once and sent in order
  (`/display/todos/<id>/done` or `/undo`) a few seconds later or on the next
  Wi-Fi visit, then the list is fetched again; Confirm sends and fetches at
  once. The calendar sync also fetches
  the list once a day or when taps wait, at most every 15 min (taps) or 3 h.

Only one task drives Wi-Fi: screen changes and deep sleep stop a running
fetch first.

## Notes from the Brain

On each Wi-Fi visit (at most hourly; at once when the calendar is open
without the bridge) the device fetches `GET /display/knowledge` for the same
weeks as `/display/weeks`. It stores one file per event and per linked note
in `/.crosspoint/deskleaf/hirn/` (`e-<hash>.txt`, `n-<hash>.txt`) and the
answer's hash plus fetch time in `hirn-stamp.txt`; an unchanged hash leaves
`hirn/` untouched. An event's key is its start (`YYYY-MM-DDTHH:MM`) and title.

The event detail page shows the termin note under "Aus dem Hirn" after the
calendar notes, and the notes it links (people, customers) as chips under
"Im Hirn". A tap on a chip opens that note's card; Back, Confirm or a tap
returns to the event.
Only one level: a card links nowhere.

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

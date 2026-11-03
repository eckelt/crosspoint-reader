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
  Confirm or a header tap switches the view, a long press on Confirm (or a
  long touch) syncs, a tap on a day in week/month opens it. Data older than
  10 minutes is synced on entry. Not shown on cover-grid home themes.

## Bridge protocol

Unchanged `calendar.payload.v1` over BLE: the device sends
`REQ:<semantic hash>:<battery mV>`, the bridge answers `TIME`, `TZ` and either
`SAME` or `LEN[F]:<n>` plus the JSON. The semantic hash matches the bridge's
`einkalSemanticHash` (contract vectors in `test/deskleaf_calendar`). The
device keeps its own timezone setting; set it to Europe/Berlin. The Deskleaf
firmware OTA and battery-history messages are not used.

Files: payload and state in `/.crosspoint/deskleaf/` on the SD card.

## Verify

- Host tests: `ARDUINOJSON_SRC=<ArduinoJson>/src ./test/deskleaf_calendar/run.sh`
- Firmware: `pio run -e x4pro`, or the `Deskleaf X4 Pro firmware` workflow
  on `deskleaf-*` branches (artifact `firmware-x4pro-deskleaf`).
- On device: bridge running, open Home → Calendar, long-press Confirm; serial
  log tag `DSK` shows `REQ sent` and `Sync: ...`. Select the Calendar sleep
  screen, sleep, and watch for `Calendar timer wake` about 2 minutes later.

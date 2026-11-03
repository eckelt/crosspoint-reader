#!/usr/bin/env bash
# Host test for lib/DeskleafCalendar. Needs ArduinoJson headers:
#   ARDUINOJSON_SRC=/path/to/ArduinoJson/src ./test/deskleaf_calendar/run.sh
# Without the variable it uses the copy PlatformIO downloads for the build.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
json="${ARDUINOJSON_SRC:-}"
if [ -z "$json" ]; then
  json="$(find "$root/.pio/libdeps" -path '*ArduinoJson/src' -type d 2>/dev/null | head -n1)"
fi
[ -n "$json" ] || { echo "ArduinoJson not found; set ARDUINOJSON_SRC" >&2; exit 2; }
out="$(mktemp -d)"
g++ -std=gnu++2a -Wall -Wextra -Werror -Wno-maybe-uninitialized -O1 -fsanitize=address,undefined \
  -I"$root/lib/DeskleafCalendar/include" -isystem "$json" \
  "$root/lib/DeskleafCalendar/src/DeskleafCalendar.cpp" "$root/test/deskleaf_calendar/DeskleafCalendarTest.cpp" \
  -o "$out/deskleaf_calendar_test"
"$out/deskleaf_calendar_test"

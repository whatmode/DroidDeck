#!/bin/bash
# EA client test: sign in (in the phone's browser), then list the EA library.
# Log: droiddeck-ea-test.log next to this script.
P=$(cd "$(dirname "$0")" && pwd)
if [ ! -t 1 ] && [ -z "${EA_IN_TERM:-}" ]; then
  export EA_IN_TERM=1
  if command -v konsole >/dev/null; then exec konsole --hold -e bash "$P/run-eatest.sh"; fi
  if command -v xterm >/dev/null; then exec xterm -hold -e bash "$P/run-eatest.sh"; fi
fi
LOG=$P/droiddeck-ea-test.log
EA=$P/bin/droiddeck-ea
open_url() {
  for o in /usr/local/bin/droiddeck-open-url xdg-open; do
    if command -v "$o" >/dev/null 2>&1 && "$o" "$1" >/dev/null 2>&1; then echo "opened with $o"; return 0; fi
  done
  echo "could not open a browser; open this address yourself:"; echo "$1"
}
{
  echo "### droiddeck-ea test $(date)"
  cat "$P/VERSION" 2>/dev/null
  echo "### status"
  "$EA" status 2>&1
  if ! "$EA" status 2>/dev/null | grep -q '"loggedIn":true'; then
    echo "### login: a browser page opens; sign in to EA there, then come back"
    "$EA" login 2>&1 | while IFS= read -r line; do
      echo "$line" | sed 's/"url":"[^"]*"/"url":"<sign-in page>"/'
      case "$line" in
        '@ea {"url":'*) url=$(printf '%s' "${line#@ea }" | python3 -c 'import json,sys; print(json.load(sys.stdin)["url"])'); open_url "$url";;
      esac
    done
  fi
  echo "### library"
  "$EA" library 2>&1 | python3 -c '
import json, sys
for line in sys.stdin:
    if line.startswith("@ea "):
        d = json.loads(line[4:])
        if "games" in d:
            print("%d games" % len(d["games"]))
            for g in d["games"]:
                print("  %-40s %-28s %s" % (g.get("name"), g.get("offerId"), g.get("slug")))
        else:
            print(line.rstrip())
    else:
        print(line.rstrip())
'
  echo "### done"
} 2>&1 | tee "$LOG"
echo
echo "Log saved to $LOG"
read -r -t 900 -p "Press Enter to close" _ || true

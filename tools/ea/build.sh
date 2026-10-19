#!/bin/bash
# Builds droiddeck-ea (maxima-lib + tools/ea/droiddeck-ea) for aarch64 Linux into $1 (default out).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "${1:-out}")
MAXIMA_REV=$(cat "$HERE/MAXIMA_REV")
WORK=$(mktemp -d)
git clone -q https://github.com/ArmchairDevelopers/Maxima "$WORK/maxima"
git -C "$WORK/maxima" checkout -q "$MAXIMA_REV"
for p in "$HERE"/patches/*.patch; do
  echo "== applying $(basename "$p")"
  git -C "$WORK/maxima" apply --whitespace=nowarn "$p"
done
cp -r "$HERE/droiddeck-ea" "$WORK/maxima/droiddeck-ea"
python3 - "$WORK/maxima/Cargo.toml" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace('members = [', 'members = [\n    "droiddeck-ea",', 1)
open(p, "w").write(s)
PY
(cd "$WORK/maxima" && cargo build --release -p droiddeck-ea)
rm -rf "$OUT" && mkdir -p "$OUT/droiddeck-ea/bin"
install -m755 "$WORK/maxima/target/release/droiddeck-ea" "$OUT/droiddeck-ea/bin/droiddeck-ea"
ln -s droiddeck-ea "$OUT/droiddeck-ea/bin/droiddeck-ea-wine"
ln -s droiddeck-ea "$OUT/droiddeck-ea/bin/maxima-bootstrap"
install -m755 "$HERE/test/run-eatest.sh" "$OUT/droiddeck-ea/run-eatest.sh"
echo "droiddeck-ea built from ${GITHUB_SHA:-local}, Maxima $MAXIMA_REV" > "$OUT/droiddeck-ea/VERSION"
(cd "$OUT" && tar czf droiddeck-ea-aarch64.tar.gz droiddeck-ea && sha256sum droiddeck-ea-aarch64.tar.gz | cut -d' ' -f1 > droiddeck-ea.sha256)
ls -l "$OUT"

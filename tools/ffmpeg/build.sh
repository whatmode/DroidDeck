#!/bin/bash
# FFmpeg 4.4 (libavcodec.so.58 / libavformat.so.58 / libavutil.so.56 ...) for aarch64 games.
# Wine's winedmo and GStreamer's gst-libav in ARM64 Proton link FFmpeg 4.x, which Proton does not
# ship, so they pick up the Steam client's cut-down copy (no h264_mp4toannexb), and MP4/H.264
# videos come out blank. This build has every decoder, demuxer, parser and bitstream filter, and
# no encoders, muxers, devices or outside libraries. droiddeck-game-env puts it first for games.
# Usage: build.sh OUT_DIR [cross-prefix]   (cross-prefix e.g. aarch64-linux-gnu-; empty = native)
set -euo pipefail
OUT=$(realpath -m "$1")
CROSS=${2:-}
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/release.env"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
git -c advice.detachedHead=false clone -q --depth 1 -b "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg "$WORK/src"
[ "$(git -C "$WORK/src" rev-parse HEAD)" = "$FFMPEG_COMMIT" ] || { echo "FFmpeg $FFMPEG_TAG is not $FFMPEG_COMMIT"; exit 1; }
cross=()
[ -n "$CROSS" ] && cross=(--enable-cross-compile --cross-prefix="$CROSS" --arch=aarch64 --target-os=linux)
cd "$WORK/src"
./configure "${cross[@]}" --prefix="$WORK/inst" \
  --enable-shared --disable-static --enable-pic \
  --disable-programs --disable-doc --disable-debug --disable-autodetect \
  --disable-avdevice --disable-postproc --disable-network \
  --disable-encoders --disable-muxers --disable-devices --disable-hwaccels --disable-outdevs --disable-indevs \
  --extra-ldflags=-Wl,-z,now >"$WORK/configure.log" || { tail -40 "$WORK/configure.log"; exit 1; }
make -j"$(nproc)" >"$WORK/make.log" 2>&1 || { tail -60 "$WORK/make.log"; exit 1; }
make install >/dev/null
mkdir -p "$OUT"
for lib in avutil.so.56 avcodec.so.58 avformat.so.58 avfilter.so.7 swscale.so.5 swresample.so.3; do
  src=$(readlink -f "$WORK/inst/lib/lib$lib")
  install -m644 "$src" "$OUT/lib$lib"
  "${CROSS}strip" --strip-unneeded "$OUT/lib$lib"
done
cp COPYING.LGPLv2.1 "$OUT/LICENSE"
echo "FFmpeg $FFMPEG_TAG ($FFMPEG_COMMIT) for DroidDeck" > "$OUT/VERSION"
ls -l "$OUT"

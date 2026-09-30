#!/bin/zsh
# bench/flash.sh [--dir fw|bench/hello] [--build-only] [--no-verify]
# stop capture -> build (BUILD_ID baked in) -> flash (retry once) -> capture into a new run
# -> wait for the boot line and verify its "build" equals the built id.
# Exit: 0 ok, 1 build/flash failed, 3 NO_BOARD, 4 no build line seen, 5 build-id mismatch.
set -u
B=${0:A:h}; ROOT=${B:h}
PY=$(ls -d /opt/homebrew/Cellar/esphome/*/libexec/bin/python 2>/dev/null | tail -1)
DIR=$ROOT/fw; BUILD_ONLY=0; VERIFY=1
while (( $# )); do
  case $1 in
    --dir) DIR=${2:A}; shift 2;;
    --build-only) BUILD_ONLY=1; shift;;
    --no-verify) VERIFY=0; shift;;
    *) echo "usage: bench/flash.sh [--dir DIR] [--build-only] [--no-verify]" >&2; exit 2;;
  esac
done
[[ -f $DIR/platformio.ini ]] || { echo "flash: no platformio.ini in $DIR" >&2; exit 1; }
if (( ! BUILD_ONLY )); then
  PORT=$($B/port.sh) || { echo "NO_BOARD"; exit 3; }
fi
rel=${DIR#$ROOT/}
sha=$(git -C $ROOT log -1 --format=%h -- $rel 2>/dev/null); [[ -z $sha ]] && sha=nogit
[[ -n $(git -C $ROOT status --porcelain -- $rel 2>/dev/null) ]] && sha=$sha-d
export BUILD_ID=${BUILD_ID:-$sha-$(date -u +%m%dT%H%M%S)}
RUN=$($PY $B/run.py _newrun flash --build-id $BUILD_ID --project $rel) || exit 1
RD=$ROOT/runs/$RUN
echo "flash: run $RUN  build_id $BUILD_ID  project $rel"

$PY $B/capture.py stop >/dev/null
echo "flash: building ..."
if ! $PY -m platformio run -d $DIR > $RD/build.log 2>&1; then
  tail -25 $RD/build.log; $PY $B/run.py _meta $RUN result=build_failed; echo "flash: BUILD FAILED (runs/$RUN/build.log)"; exit 1
fi
BIN=$(ls -t $DIR/.pio/build/*/firmware.bin | head -1)
FACTORY=${BIN:h}/firmware.factory.bin   # merged bootloader+partitions+app, flashed at 0x0
[[ -f $FACTORY ]] || { echo "flash: no firmware.factory.bin next to $BIN"; exit 1; }
if ! LC_ALL=C grep -aq -- "$BUILD_ID" $BIN; then
  $PY $B/run.py _meta $RUN result=build_id_not_in_binary
  echo "flash: BUILD_ID '$BUILD_ID' not found in $BIN — is -DBUILD_ID wired in build_flags?"; exit 1
fi
echo "flash: build ok ($(grep -E 'RAM:|Flash:' $RD/build.log | tr -s ' ' | tr '\n' ' '))"
(( BUILD_ONLY )) && { $PY $B/run.py _meta $RUN result=build_only; echo "flash: build-only done"; exit 0; }

ok=0
for attempt in 1 2; do
  PORT=$($B/port.sh) || { $PY $B/run.py _meta $RUN result=no_board; echo "NO_BOARD"; exit 3; }
  echo "flash: upload attempt $attempt on $PORT"
  if $PY -m esptool --chip esp32c6 --port $PORT --baud 460800 --connect-attempts 3 --after hard-reset \
       write-flash 0x0 $FACTORY >> $RD/flash.log 2>&1; then ok=1; break; fi
  tail -5 $RD/flash.log; sleep 2
done
(( ok )) || { $PY $B/run.py _meta $RUN result=flash_failed; echo "flash: UPLOAD FAILED twice (runs/$RUN/flash.log)"; exit 1; }
$PY $B/capture.py start --run $RUN >/dev/null || { echo "flash: capture start failed"; exit 1; }
if (( ! VERIFY )); then $PY $B/run.py _meta $RUN result=flashed_unverified; echo "flash: flashed (unverified), capture running for $RUN"; exit 0; fi
out=$($PY $B/run.py _waitbuild $RUN --build $BUILD_ID --timeout 15); rc=$?
echo "flash: $out"
if (( rc != 0 )); then
  # boot line may have been lost across re-enumeration: one hard reset, then look again
  echo "flash: no match yet — one hard reset and re-check"
  $PY $B/capture.py stop >/dev/null
  $PY $B/run.py _event $RUN '{"ev":"bench.reset","via":"esptool","why":"verify"}'
  $PY $B/run.py _hardreset
  $PY $B/capture.py start --run $RUN >/dev/null
  out=$($PY $B/run.py _waitbuild $RUN --build $BUILD_ID --timeout 15); rc=$?
  echo "flash: $out"
fi
res=ok; (( rc == 4 )) && res=no_build_line; (( rc == 5 )) && res=build_mismatch
$PY $B/run.py _meta $RUN result=$res
print -r -- "{\"build_id\":\"$BUILD_ID\",\"run\":\"$RUN\",\"project\":\"$rel\",\"result\":\"$res\"}" > $ROOT/runs/.last_flash.json
$PY $B/run.py _summary $RUN
echo "flash: capture keeps running for $RUN (bench/capture.py stop to free the port)"
exit $rc

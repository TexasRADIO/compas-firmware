#!/bin/sh
# Run a checksum-pinned FM-1 emulator in a container with no device access.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
emu="${COMP_EMU:?Set COMP_EMU to the unpacked Linux x86_64 fm1-emu executable}"
docker="${COMP_DOCKER:-docker}"
variant="${1:-stress}"
printf '%s  %s\n' 6d7ab12b36f63e0f6cf7b07d6700004a3f434b70e2c5504aff261bb293fe658e "$emu" | shasum -a 256 -c >/dev/null
dump="$(python3 tools/board-emu.py build/board "$variant" prepare)"
addr="${dump% *}"
words="${dump#* }"
folder=/out
quiet=--quiet
image="emu-$variant.xip"
case "$variant" in
    demo)
        folder=/out/demo
        quiet=
        image=../emu-demo.xip
        mkdir -p build/board/demo/work
        set -- --ms 9800 --wav /out/demo/work/stage.wav \
            --snap 350,850,1100,1350,1550,1800,2050,2300,2550,2800,3050,3300,3550,3800,4050,4300,4550,4800,5050,5300,5550,5800,6050,6300,6550,6800,7050,7300,7550,7800,8050,8300,8550,8800,9050,9300,9550 \
            --keys '850-950:GLO,1050:pot:666,1150-1250:n6,1450-1550:PLAY,2700-2800:ARP,4100-4200:n3,9200-9300:PLAY'
        ;;
    master)
        folder=/out/master
        quiet=
        image=../emu-master.xip
        mkdir -p build/board/master/work
        set -- --ms 3650 --wav /out/master/work/master.wav \
            --snap 350,1490,1990,2520,3120,3540 \
            --keys '850-950:GLO,1000:pot:0,1130-1230:n6,1350-1450:PLAY,1650:pot:760,2210:pot:0,2720:pot:512,3370-3470:PLAY'
        ;;
    stress) set -- --ms 5500 --snap 3300,5250 ;;
    piano) set -- --ms 4400 --snap 1750,3350,4100 --keys '850-950:GLO,1050:pot:666,1150-1250:n8,1450-1550:PLAY,3000-3100:ARP,3300-3400:HOME,3500-3600:n8' ;;
    golondrina) set -- --ms 16800 --snap 2000,3600,16000 --keys '850-950:GLO,1050:pot:666,1150-1250:n9,1300-1380:HOME,1430-1510:n8,1560-1640:PLAY' ;;
    offline) set -- --ms 5150 --snap 350,1050,3000,3900,4900 --keys '500-625:PLAY,1050:pot:410,1250-1350:PLAY,1500-1600:GLO,1750-1850:n5,2000-2100:PLAY,2250-2350:n2,2450-2550:ARP' ;;
esac
mkdir -p build/board/work
"$docker" run --rm --pull=never --platform linux/amd64 --network none --cap-drop ALL --pids-limit 32 \
    --mount "type=bind,source=$emu,target=/emu/fm1-emu,readonly" \
    --mount "type=bind,source=$root/build/board,target=/out" -w "$folder" -e FM1_STRICT=1 \
    sha256:2d0e361d19a6da61a8587d6c2f2c1517d0389fd72d64930cd16980f8662afac3 \
    /emu/fm1-emu --bin "$image" --entry 0x02000120 ${quiet:+"$quiet"} --dump "$addr" "$words" "$@" \
    >"build/board/emu-$variant.log" 2>&1
python3 tools/board-emu.py build/board "$variant" "build/board/emu-$variant.log" >"build/board/emu-$variant.json"
cat "build/board/emu-$variant.json"

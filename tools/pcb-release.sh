#!/usr/bin/env bash
# PCB release zip for one board, into <out dir>:
#   xbx-<board>-pcb-rev<REV>.zip  Gerbers, BOM, positions, netlist, iBOM
# Every file in it is named after the Gerber zip (<title>_Rev<REV>_*), so
# files from different boards or revisions can't get mixed up.
#
# Usage: tools/pcb-release.sh <controller|dongle> <out dir>
# Needs, exported from the committed board (both untracked; their dates are
# listed to check they're current):
#   pcb/<project>/production/  JLC Fabrication Toolkit output
#   pcb/<project>/bom/         InteractiveHtmlBom output (*-ibom.html)
# and pcb/<project>/bom-digikey.csv, checked against the schematic's DigiKey
# fields (kicad-cli). ALLOW_DIRTY=1 packs uncommitted board files.

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { echo "usage: $0 <controller|dongle> <out dir>" >&2; exit 2; }
[ $# -eq 2 ] || usage
case "$1" in
controller) project=xbx-nrf ;;
dongle) project=xbx-nrf-dongle ;;
*) usage ;;
esac
board="$1"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
dir="$repo/pcb/$project"

rev=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["text_variables"]["REV"])' \
	"$dir/$project.kicad_pro")
name="xbx-$board-pcb-rev$rev"

# the files must come from the committed board
if [ -n "$(git -C "$repo" status --porcelain -- "pcb/$project/$project.kicad_pcb" \
	"pcb/$project/$project.kicad_sch" "pcb/$project/bom-digikey.csv")" ] &&
	[ -z "${ALLOW_DIRTY:-}" ]; then
	echo "error: uncommitted changes in pcb/$project/ (ALLOW_DIRTY=1 to pack anyway)" >&2
	exit 1
fi
commit=$(git -C "$repo" rev-parse --short HEAD)

one() { # exactly one file matching a glob; its date shown to check it's fresh
	local f
	f=$(compgen -G "$1") || { echo "error: missing $1" >&2; exit 1; }
	[ "$(echo "$f" | wc -l)" -eq 1 ] || { echo "error: several $1" >&2; exit 1; }
	echo "  $(date -r "$f" '+%F %R')  ${f#"$repo"/}" >&2
	echo "$f"
}
echo "board: $(date -r "$dir/$project.kicad_pcb" '+%F %R')  pcb/$project/$project.kicad_pcb" >&2
gerbers=$(one "$dir/production/*_Rev$rev.zip")
positions=$(one "$dir/production/*_positions.csv")
netlist=$(one "$dir/production/netlist.ipc")
ibom=$(one "$dir/bom/*-ibom.html")
bom="$dir/bom-digikey.csv"

# bom-digikey.csv against the schematic: same DigiKey number per designator
if command -v kicad-cli >/dev/null; then
	sch_bom="$out/.$name-sch.csv"
	kicad-cli sch export bom --fields 'Reference,DigiKey' --group-by '' \
		-o "$sch_bom" "$dir/$project.kicad_sch" >/dev/null
	python3 - "$bom" "$sch_bom" <<'EOF' || { rm -f "$sch_bom"; exit 1; }
import csv, sys
ours = {}
for r in csv.DictReader(open(sys.argv[1], encoding="utf-8-sig")):
    if r["Designator"] != "-":
        for d in r["Designator"].split(","):
            ours[d.strip()] = r["DigiKey"]
sch = {r["Reference"]: r["DigiKey"]
       for r in csv.DictReader(open(sys.argv[2], encoding="utf-8-sig"))}
bad = [f"{d}: csv {ours.get(d)!r}, schematic {v!r}"
       for d, v in sorted(sch.items()) if v != "N/A" and ours.get(d) != v]
bad += [f"{d}: not in the schematic" for d in sorted(set(ours) - set(sch))]
if bad:
    print("error: bom-digikey.csv doesn't match the schematic:", *bad, sep="\n  ",
          file=sys.stderr)
    sys.exit(1)
EOF
	rm -f "$sch_bom"
else
	echo "warning: no kicad-cli; BOM not checked against the schematic" >&2
fi

stage="$out/.$name"
rm -rf "$stage"
mkdir -p "$stage/$name"
trap 'rm -rf "$stage"' EXIT
# every file named for its board and revision, e.g. XBX-NRF_Gamepad_Rev1.1_*
p="$stage/$name/$(basename "$gerbers" .zip)"
cp "$gerbers" "${p}_gerbers.zip"
cp "$bom" "${p}_bom-digikey.csv"
cp "$positions" "${p}_positions.csv"
cp "$netlist" "${p}_netlist.ipc"
cp "$ibom" "${p}_ibom.html"
cp "$repo/LICENSES/CERN-OHL-S-2.0.txt" "${p}_LICENSE.txt"
p=$(basename "$p")
cat >"$stage/$name/${p}_README.txt" <<EOF
xbx-nrf $board PCB, Rev$rev (from commit $commit)

${p}_gerbers.zip
    Upload to the PCB fab
${p}_bom-digikey.csv
    Parts, with DigiKey part numbers
${p}_positions.csv
    Part positions (pick and place)
${p}_netlist.ipc
    Optional: the fab's electrical test
${p}_ibom.html
    Interactive BOM: open in a browser to place parts

Assembly guide: https://github.com/zuidec/xbx-nrf/blob/main/docs/hardware_assembly.md

Copyright (c) 2026 zuidec. Licensed under CERN-OHL-S-2.0 (${p}_LICENSE.txt).
Source location: https://github.com/zuidec/xbx-nrf (pcb/$project/)
EOF

rm -f "$out/$name.zip"
(cd "$stage" && zip -qr "$out/$name.zip" "$name")
echo "$out/$name.zip"

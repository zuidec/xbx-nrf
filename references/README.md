# References

Our own files (in the repo, under the project licence):

| File | What |
|---|---|
| `pcb-cad.FCStd` | FreeCAD outline sketch of the bottom board |
| `pcb-cad-pcbSketch.dxf` | DXF export of the outline (for KiCad `Edge.Cuts`) |
| `pcb-cad-pcb.step` | STEP export |

The stick symbol, footprint and 3D model in `pcb/xbx-nrf/lib/RKJXV122400R/` are
SnapMagic files under CC BY-SA 4.0 (Design Exception); see the `ATTRIBUTION.md`
there.

## Third-party documents

Kept locally in `references/third-party/`, which is **git-ignored**: they belong
to their authors and aren't covered by this project's licence, so they're not
redistributed. Download them into that folder from the sources below.

| File | What | Source |
|---|---|---|
| `XB1 1914 TOP BOARD.pdf` | Top board schematic (RDC) | [AcidMods XB1 PCB scans thread][acidmods] |
| `XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf` | Bottom board schematic (RDC) | [AcidMods][acidmods] |
| `XB1_1914_SOC_SOME_VALUES.pdf` | SoC module schematic (RDC) | [AcidMods][acidmods] |
| `1914 BBB.jpg`, `1914 BBT.jpg`, `1914 TBB.jpg`, `1914 TBT.jpg` | Board scans (bottom/top board, both sides) | [AcidMods][acidmods] |
| `1914_SOC_*.jpg` | SoC module scans, with/without designators | [AcidMods][acidmods] |
| `controller-pinouts.txt`, `soc-pinout.txt` | J3/J5 pinout, test points, SoC pinout | [KasynParts 1914 scans & info][kasyn] |
| `sketchify-edges.png` | Edge-traced board image (derived from the scans) | Made from the scans above |
| `A1304-Datasheet.pdf` | Allegro A1304 (trigger Hall sensor) | [Allegro][a1304] |
| `A3910-datasheet.pdf` | Allegro A3910 (rumble driver) | [Allegro][a3910] |
| `PCAL6416A-datasheet.pdf` | NXP PCAL6416A (button expander) | [NXP][pcal] |
| `product_catalog_rkjxv.pdf` | Alps RKJXV stick catalog | [Alps Alpine][alps] |
| `windows_protocols-ms-gipusb.pdf` | MS-GIPUSB (Xbox GIP over USB) | [Microsoft Learn][gip] (PDF download on the page) |

[acidmods]: https://acidmods.com/forum/index.php?topic=44547.0
[kasyn]: https://kasynparts.com/xbox-series-xs-controller-1914-motherboard-pcb-scans-info/
[a1304]: https://www.allegromicro.com/en/products/sense/linear-and-angular-position/linear-position-sensor-ics/a1304
[a3910]: https://www.allegromicro.com/-/media/files/datasheets/a3910-datasheet.pdf
[pcal]: https://www.nxp.com/docs/en/data-sheet/PCAL6416A.pdf
[alps]: https://tech.alpsalpine.com/e/products/detail/RKJXV122400R/
[gip]: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/

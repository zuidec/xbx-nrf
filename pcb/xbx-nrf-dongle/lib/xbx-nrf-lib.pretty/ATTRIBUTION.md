# xbx-nrf-lib: dongle project footprints

Footprints used by the dongle board, saved from it into this project library.
Each one is a copy of an existing footprint, so it keeps that footprint's
license.

## From KiCad's libraries

Source: KiCad 10 footprint libraries (<https://gitlab.com/kicad/libraries>).
License: [CC BY-SA 4.0][cc] with the [KiCad libraries exception][exc]: using
them in a design doesn't put the design under CC BY-SA.

| File | Source | Modified |
|---|---|---|
| `Raytac_MDBT50Q.kicad_mod` | `RF_Module:Raytac_MDBT50Q` | Multi-layer antenna keep-out zone and its note removed (the U1MV2 module uses its u.FL connector, not an on-board antenna); board fields (reference, value, rotation, description) |
| `Crystal_SMD_3215-2Pin_3.2x1.5mm.kicad_mod` | `Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm` | Board fields only |
| `SolderJumper-3_P2.0mm_Open_TrianglePad1.0x1.5mm_NumberLabels.kicad_mod` | `Jumper:` same name | Board fields only |
| `TestPoint_Pad_1.0x1.0mm-NoSilkscreen.kicad_mod` | `TestPoint:TestPoint_Pad_1.0x1.0mm` | Renamed; silkscreen outline removed, reference hidden; board fields |
| `TestPoint_Pad_1.5x1.5mm-NoSilkscreen.kicad_mod` | `TestPoint:TestPoint_Pad_1.5x1.5mm` | Renamed; reference hidden, value shown on the silkscreen; board fields |

## From SnapMagic

| File | Source | Modified |
|---|---|---|
| `USB_C_Receptacle_16P_Amphenol_10155435-00011LF.kicad_mod` | `../USB_C_Receptacle_16P_Amphenol_10155435-00011LF/` (SnapMagic, see its `ATTRIBUTION.md` for the original changes) | Board fields only |

License: [CC BY-SA 4.0][cc] with the SnapMagic Design Exception 1.0.

[cc]: https://creativecommons.org/licenses/by-sa/4.0/
[exc]: https://spdx.org/licenses/KiCad-libraries-exception.html

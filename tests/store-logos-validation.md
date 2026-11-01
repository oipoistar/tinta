# The Store logos are sharp at every display scale (#309)

Validated on Windows 11 with the Windows 10 SDK (10.0.26100) on 2026-11-01.

## What was wrong

- **The logos were enlarged from a small icon.** Every PNG in
  `packaging/msix/Assets` had soft edges: the band between the icon and the
  transparency was 8 px wide in the 150 px tile logo and 16 px in the 300 px
  one, against 2.3 px in the 256 px icon in `resources/tinta.ico`, and the
  icon's white outline spread into a pale halo. The reporter's Start tile on
  Windows 10 shows the same smear.
- **Only the scale-100 logos reached Windows.** `pack.ps1` kept makepri's
  default config, which moves every scale but 100 into a resource pack
  (`resources.scale-200.pri`) that only a bundle installs. The Store gets a
  single package, so its `resources.pri` listed the scale-100 logos alone,
  and Windows enlarged the 150 px tile logo at 125, 150 and 200%. The Store
  package installed here (3.9.6.0) has the same index.

## The fix

- `packaging/msix/make_assets.py` makes each logo from the 256 px icon,
  resized in premultiplied alpha, at the size its name asks for and with
  the icon placed as before. The eight logos together are 93 KB, down from
  183 KB.
- `pack.ps1` drops the config's resource packs, so `resources.pri` lists
  every scale.

## Checks

- `python packaging/msix/make_assets.py --check` compares the committed
  logos with fresh ones (size from the name, and pixels within 2 of
  the resized icon): 8 failures on the old logos, none now.
- Edge band, old to new: tile logo 8.3 to 2.4 px at 150 and 15.8 to 2.6 px
  at 300; the 256 px taskbar logo 13.6 to 2.3 px; the 44 to 100 px logos
  3.4 to 6.0 px down to 2.6 to 2.8 px.
- The medium tile as Windows 10 draws it, the scale-100 logo at 100 px and
  the scale-200 logo at 125 and 150 px on a grey tile, before and after:
  `out/msix-309/before-after.png` (ignored).
- The package packed with the old and the new `pack.ps1` under Windows
  PowerShell (a copy saved with a BOM), its `resources.pri` dumped with
  `makepri dump`: the old one lists Square150x150Logo, Square44x44Logo and
  StoreLogo at scale 100 only, beside a `resources.scale-200.pri`; the new
  one lists both scales of each and no other index. The
  `targetsize` logos are in both.

## Limitations

- The Start tile itself was not seen: this machine runs Windows 11, whose
  Start has no tiles, at 100%. The shell resolved the installed package's
  logos to their scale-100 files there, as it should at 100%.
- The 300 px tile logo is the 256 px icon enlarged 1.17 times; it is sharp
  but would be sharper from a larger or vector source, which the repository
  does not have.
- Users see it once the next release is submitted to the Store; the portable
  `tinta.exe` and its icon are unchanged.

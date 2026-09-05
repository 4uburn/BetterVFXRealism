# Better VFX Realism

Auburn's Arma Reforger VFX addon, with a team contribution for denser surface dust, smaller fragment wisps, coherent wind response and lingering indoor fine particulate.

Open `BetterEffectsRealism.gproj` in Workbench. The existing addon ID, GUID and BER resource identities are preserved; the display name is **Better VFX Realism**.

See [the review guide](docs/VFX-REVIEW.md) for the complete code audit, physics sources, implementation decisions, validation evidence and live review matrix. This is a review candidate, not a tested Workshop release.

The [video comparison and debris model](docs/VIDEO-REFERENCE-REVIEW.md) explains the supplied references, M242/AP/HE changes, bounded debris maths, and the distinction between enabled large-blast condensation and unimplemented heat distortion.

Run the read-only data checks with Python 3:

```text
python tools/validate_vfx.py
```

No additional Python packages are needed. Compile with Workbench separately; data checks cannot establish rendered or multiplayer behaviour.

This contribution builds on Auburn's existing work. Existing base-game references remain subject to Bohemia's terms; no Squad assets or code were added.

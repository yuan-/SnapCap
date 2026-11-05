# Changelog

Notable changes to SnapCap, newest first. Downloads are on the [Releases](../../releases) page.

## 0.2.0 — 2026-11-05

### Added

- **Mouse and keyboard display in recordings.** A new button on the recording bar draws your input
  into the video: a ring at each click (its own color for left, right and middle), an optional
  highlight that follows the pointer, an optional mouse badge showing the held button and the
  wheel's direction, and a caption of the keys you press (`Ctrl + Shift + S`; pressing it again
  counts up as `×3`). It is drawn only into the file, never on screen, and can be switched on or
  off mid-recording.
  - By default only shortcuts and special keys are shown, so ordinary typing — passwords
    included — never ends up in the video. Choose **All keys** to show typed text as well.
  - A new **Preferences → Recording → Mouse and keyboard** section: an on/off switch (the same
    switch as the recording-bar button), plus the caption's position, sizes, colors, background
    opacity and how long each key stays.

### Changed

- **Line tool:** a single line drawn with press-drag-release is now committed on release, in both
  the snip overlay and the pin editor, like the other drag tools. Lines placed by clicking
  (multi-segment) still finish with right-click.
- **Right-click on a selected shape** in the snip overlay — a rectangle, ellipse, marker
  rectangle, mosaic or blur, e.g. right after drawing it — now confirms it and drops the
  selection, like Esc, instead of closing the capture. Alt+right-click quick-mark is unchanged.
- **Right-click no longer closes a capture you are annotating.** Once the region is committed, a
  right-click with nothing to finish or deselect does nothing; close the capture with Esc or the
  toolbar's close button. While you are still choosing the region, right-click still cancels.

## 0.1.0 — 2026-10-20

Initial public release. See the [README](README.md) for what SnapCap does.

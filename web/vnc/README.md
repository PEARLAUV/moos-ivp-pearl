# PEARL noVNC web client

This directory contains noVNC 1.7.0 runtime files vendored from the Gentoo
`www-apps/novnc` package. See `VERSION` and `LICENSE.txt`.

PEARL-specific changes are intentionally limited to:

- `index.html`: page title, PEARL logo text, and inclusion of `pearl.css`
- `pearl.css`: branding colors and logo styling
- `defaults.json`: connection and display defaults
- `app/ui.js`: fixed PEARL document title after connecting

The default settings enable local scaling, automatic connection, and automatic
reconnection every second. Other upstream files under `app/`, `core/`, and
`vendor/` should remain unmodified to make future noVNC updates easier.

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

## Launcher bypass

Set `MOOS_VNC_DISABLE=true` when invoking `scripts/moos-vnc-session` to skip
TigerVNC, Openbox, websockify, noVNC, and Tailscale setup. The launcher directly
replaces itself with the configured program and preserves every program
argument:

```sh
MOOS_VNC_DISABLE=true moos-vnc-session targ_shoreside.moos pMarineViewer
# Equivalent to: pMarineViewer targ_shoreside.moos pMarineViewer
```

This also works with `--prog`; for example,
`MOOS_VNC_DISABLE=true moos-vnc-session --prog alogview mission.alog` directly
executes `alogview mission.alog`. Bypass mode does not require VNC dependencies
and permits programs that take no arguments.

# web/ — reference only (Phase 0)

This folder is **not** the StudioKEYS Micro companion product.

- `editor.html` and the scripts beside it are the picture of the editor pages and the
  USB SysEx protocol proof (`EDITOR_PROTOCOL.md`).
- `web/emu/` proves the firmware C already runs off the chip (WASM / `native_check`).

Nothing here is launched by the plugin build, shipped inside the VST3, or drawn in a
WebView. The product window lives under `plugin/StudioKEYS/`.

Do not add a build step that opens `editor.html` or the emu page as the companion.

# Changelog

## 2.0.1 — 2026-07-30

### Changed

- Made the PocketFrame documentation self-contained and product-independent.
- Clarified release, installation, and local-data documentation.

## 2.0.0 — 2026-07-30

### Added

- PocketFrame custom e-ink renderer for native PocketBook applications.
- Square header actions and consistent Back navigation.
- Dedicated server management screen with explicit Edit and two-tap Delete.
- Catalog search, pagination, metadata view, and bottom download action.
- Dedicated `/applications/PocketOPDSData` configuration directory.
- Automatic migration of legacy `PocketOPDS.cfg*` files.
- Stable multi-step keyboard workflow for server configuration.
- Windows double-click build wrapper that preserves build output.

### Changed

- Replaced firmware `OpenList` navigation with application-owned rendering.
- Standardized typography, header geometry, margins, and row height.
- Restored complete OPDS parsing and networking sources.
- Improved Docker diagnostics in the PowerShell build script.

### Fixed

- Keyboard disappearing during repaint or between consecutive fields.
- First-launch framebuffer offset and blank top strip.
- Incomplete `opds.c` and `net.c` source files.
- Build window closing before errors could be read.

# PocketOPDS

> A native OPDS catalog browser for PocketBook e-readers, built with the InkView SDK.

![Version](https://img.shields.io/badge/version-1.0.0-red)
![License](https://img.shields.io/badge/license-MIT-blue)
![Platform](https://img.shields.io/badge/platform-PocketBook%20InkView-green)

---

## Overview

PocketOPDS runs natively on PocketBook devices and lets you browse any OPDS server directly from your e-reader. Navigate catalogs, search by keyword, and download books straight to your library — without leaving the device.

---

## Features

| Feature | Description |
|---|---|
| **Multi-server** | Add, edit, and delete multiple OPDS servers with per-server credentials |
| **Catalog browsing** | Navigate category hierarchies with proper back/home navigation |
| **Full-text search** | OpenSearch description URL support — works with Calibre, Kavita, and others |
| **One-tap download** | Single-format books download immediately; multi-format shows a picker |
| **Organized storage** | Books saved to `Books/<Author>/<Title>.<ext>` — same layout as Calibre |
| **Instant library scan** | PocketBook library is updated immediately after each download |
| **HTTP Basic Auth** | Per-server username and password, stored in the device config |
| **Pagination** | Load-more row appends next page in-place without leaving the list |

---

## Requirements

- A PocketBook e-reader running firmware 6.x
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (to build)

Developed and tested on the **PocketBook Verse Pro (B300, 300 DPI)**.

---

## Build

The build environment is fully containerized — the PocketBook SDK and ARM cross-compiler are installed automatically inside Docker.

**First time only** (downloads the SDK, ~540 MB):
```bash
docker build -t pocketopds-builder .
```

**Compile:**
```bash
# Linux / macOS
docker run --rm -v "$(pwd):/workspace" pocketopds-builder

# Windows (PowerShell)
docker run --rm -v "${PWD}:/workspace" pocketopds-builder
```

Output: `build/PocketOPDS.app`

---

## Installation

1. Connect your PocketBook via USB.
2. Copy `build/PocketOPDS.app` to the applications folder on the device:
   ```
   /mnt/ext1/applications/PocketOPDS.app
   ```
3. Safely eject the device.
4. **PocketOPDS** will appear in the Applications section of the home screen.

---

## Usage

1. Open **PocketOPDS** from Applications.
2. Tap **+ Add server…** and enter a name, OPDS URL, and optional credentials.
3. Tap a server to open its root catalog.
4. Navigate categories, or tap the **Search** row if the server supports OpenSearch.
5. Tap a book to download it — files go to `/mnt/ext1/Books/<Author>/`.
6. Long-press a server row to **Edit** or **Delete** it.

---

## Technical Notes

- **Language**: C (C11), ~1700 lines across 8 source files
- **SDK**: PocketBook InkView SDK 6.8 (B300)
- **Networking**: libcurl (HTTPS, Basic Auth, redirect following)
- **Parsing**: expat (namespace-aware Atom/OPDS XML)
- **Config**: InkView key-value config API — stored at `/mnt/ext1/applications/PocketOPDS.cfg`
- **Build system**: CMake + Docker (Ubuntu 22.04, arm-obreey-linux-gnueabi-gcc 6.3)

---

## Contributing

Contributions, bug reports, and feature suggestions are welcome.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature`)
3. Commit your changes (`git commit -m 'Add your feature'`)
4. Push to the branch (`git push origin feature/your-feature`)
5. Open a Pull Request

---

## License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for full terms.


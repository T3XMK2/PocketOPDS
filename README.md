# PocketOPDS

PocketOPDS is a native OPDS catalog browser and book downloader for PocketBook
e-readers. It is designed for slow e-ink displays, physical page keys, and
touch interaction without requiring KOReader or a web browser.

The current interface uses **PocketFrame**, a monochrome UI system designed
specifically for slow e-ink displays. Headers, square action buttons,
typography, spacing, scrolling, errors, and confirmation states are drawn by
the application instead of the firmware `OpenList` widget.

## Features

- Add and manage multiple OPDS servers.
- Optional HTTP Basic Authentication.
- Browse nested OPDS catalogs.
- Search catalogs that expose OpenSearch.
- Follow paginated feeds with **Load more**.
- View book metadata and summaries.
- Download EPUB, PDF, MOBI, FB2, and other supported formats.
- Notify the PocketBook library scanner after a download.
- Touch controls and physical navigation-key support.
- Local configuration stored in a dedicated data directory.

PocketOPDS has been developed primarily for the PocketBook Verse Pro and the
PocketBook SDK 6.8/B300 toolchain.

## Installation

1. Build or download `PocketOPDS.app`.
2. Connect the PocketBook to the computer by USB.
3. Copy the application to:

   ```text
   /mnt/ext1/applications/PocketOPDS.app
   ```

4. Safely disconnect the device and launch **PocketOPDS** from the Applications
   screen.

Only the `.app` file belongs in the Applications root. PocketOPDS creates its
configuration directory automatically:

```text
/mnt/ext1/applications/PocketOPDSData/
└── settings.cfg
```

Older `PocketOPDS.cfg*` files are migrated into this directory on first launch.
Downloaded books are stored in `/mnt/ext1/Books`.

## Adding a server

Tap the `+` button and enter:

1. Server name
2. OPDS URL
3. Username, if required
4. Password, if required

For a Calibre Content Server on the same network, the URL normally looks like:

```text
http://192.168.1.12:8080/opds
```

Use the computer's LAN address. `localhost` and `127.0.0.1` refer to the
PocketBook itself and will not reach the computer.

Tap a server row to open its catalog. Tap the square pencil button on the row
to review, edit, or delete that server. Deletion requires a second tap on the
highlighted trash button.

## Building on Windows

Requirements:

- Docker Desktop
- Docker Engine running with Linux containers

Run:

```text
Build-PocketOPDS.cmd
```

The first build creates a Docker image containing the PocketBook SDK. Later
builds reuse that cached image. The resulting application is written to:

```text
build/PocketOPDS.app
```

To recreate the SDK image:

```powershell
.\build.ps1 -Rebuild
```

## Building on Linux

Extract the PocketBook SDK B300 6.8, point `PBSDK` to the directory containing
`SDK-B300-6.8`, then run:

```bash
export PBSDK=/opt/pocketbook-sdk
chmod +x build.sh
./build.sh
```

Alternatively, use the Dockerfile directly:

```bash
docker build -t pocketopds-builder .
docker run --rm -v "$PWD:/workspace" pocketopds-builder
```

## PocketFrame UI

PocketOPDS uses these core PocketFrame layout values:

| Element | Value |
|---|---:|
| Outer margin | 16 px |
| Header action | 64 × 64 px |
| Action gap | 8 px |
| Standard row | 88 px |
| Body font | screen height / 52, clamped to 22–42 px |
| Small font | body font − 7 px, minimum 18 px |
| Title font | body font + 8 px, bold |
| Header height | body font × 2 + 28 px |

The firmware panel is disabled with `SetPanelType(0)` so the renderer uses the
real framebuffer origin and avoids blank or wrapped bands on first launch.
See [POCKETFRAME.md](POCKETFRAME.md) for the design rules.

## Project structure

```text
src/
├── pocketopds_ui.cpp  Custom PocketFrame renderer and navigation
├── config.c/.h        Persistent server configuration and migration
├── net.c/.h           Wi-Fi and libcurl networking
└── opds.c/.h          OPDS/Atom parsing and URL resolution
```

The repository contains only the active custom renderer; the obsolete
`OpenList` implementation has been removed.

## Local data and privacy

PocketOPDS does not upload its configuration anywhere. Server addresses and
optional credentials are stored locally in:

```text
/mnt/ext1/applications/PocketOPDSData/settings.cfg
```

Credentials are stored as plain text because the PocketBook InkView
configuration API does not provide a secure credential store. Treat the device
and its USB storage as sensitive if authenticated OPDS servers are configured.

## Troubleshooting

### The server works on the computer but not on PocketBook

- Confirm both devices are connected to the same LAN.
- Test the OPDS URL from a phone on the same Wi-Fi.
- Allow the server's TCP port through the computer firewall for private/local
  networks.
- Avoid guest Wi-Fi networks that isolate clients.
- Use the computer's LAN IP, not `localhost`.

### A keyboard closes immediately

Current builds suspend application repainting while the InkView keyboard is
active and defer transitions between fields. Rebuild and replace the `.app` if
an older build still shows this behavior.

### The first screen is shifted or has a blank strip

Current builds disable the PocketBook firmware panel before measuring and
drawing the interface. Make sure the installed `.app` is the latest build.

## License

No license file is currently included. Add an explicit license before
redistributing modified builds or accepting external contributions.

# Clientre 5 Quick Start

> For flashing the device, see the [README](../README.md#build-and-flash).

## 1. First boot

1. Insert a TF card and power on.
2. Pick a language on the first screen. You can change it later in **Settings → System → Language**
   (the device restarts).

## 2. WiFi

On the **WiFi** tile, tap a scanned network or enter an SSID and password manually. Leave the password
empty for open networks. Once connected, the clock is synced over NTP and written back to the RTC.

## 3. SSH terminal

1. Open **SSH Terminal → +** and add a server (name, host, port, user, and a password or a private key on the TF card).
2. Tap the card to connect. On the first connection, check the host key fingerprint and trust it (stored in NVS).
3. Screen-edge hot zones: **top** = status bar, **left** = control bar (ESC/TAB/CTRL/ALT/arrows/HOME/END/PgUp/PgDn/F-keys/^C/^Z/^D), **right** = soft keyboard.
4. Scroll back by dragging the screen up and down, or with Shift+PgUp/PgDn.

## 4. Japanese input

| Key | Action |
|---|---|
| Ctrl+Space / `A/あ` soft key | Switch input source (English → Japanese → English) |
| Romaji | Converted to hiragana as you type (`kyou`→きょう, `nn`→ん, `kk`→っk) |
| Space | Start conversion / next candidate; `1`–`9` picks, PgUp/PgDn pages |
| Enter | Commit (sent to the host as UTF-8) |
| Esc | Cancel conversion / discard input |
| F6 / F7 / F8 | Commit as hiragana / katakana / half-width katakana |
| `,` `.` `[` `]` `!` `?` | Become 、。「」！？ in Japanese mode |
| Settings → Input → Japanese | Default to hiragana or katakana |

SKK-JISYO.M is built in. For a larger dictionary, put `/skk/SKK-JISYO.L` on the TF card and turn on
**Settings → Input → SD: /skk/SKK-JISYO.L** (loaded into PSRAM).
When the UI language is Chinese, pinyin input is added to the switching order.

## 5. Physical keyboard

When a Tab5 keyboard (I2C 0x6D) or a USB keyboard is detected, the soft keyboard stops appearing
automatically. Ctrl+letter, Alt+letter, arrows, Home/End and F1–F12 go straight to the terminal.
The keyboard LED colour and brightness can be changed in Settings.

The whole UI can be driven from the keyboard. The focused item is outlined and off-screen items scroll into view.

| Key | Action |
| --- | --- |
| Tab / Shift+Tab | Next / previous item |
| Enter / Space | Press a button, toggle a switch |
| Esc | Back, cancel a dialog |
| Arrows | Move between items; move the cursor in text fields; change a slider |
| Shift+Arrow | Change a slider in steps of 10 |
| PageUp / PageDown | Scroll a list |
| Ctrl+Enter | Long-press menu of the focused item |
| Ctrl+Tab (in SSH) | Toggle between SSH input and UI navigation; Esc in UI navigation also returns to SSH |
| Shift+PageUp / Shift+PageDown (in SSH) | Scroll the terminal history |

Text fields accept normal typing and IME conversion; Tab moves to the confirm button. While typing into SSH,
Tab and Esc are sent to the host, except while a confirmation dialog is open.

## 6. Files, images, text, music and camera

- **Files**: browse the TF card and USB sticks. Long-press to rename, copy, cut, delete or select; the + button
  at the top right creates, pastes and selects all.
- **Images**: JPEG (hardware decoding, large images scaled in software), PNG/BMP/GIF. EXIF info, swipe for
  previous/next, zoom and rotate.
- **Text**: UTF-8 / UTF-16 files in a monospace font covering JIS X 0208, JIS X 0213 levels 3/4, half-width
  kana, compatibility ideographs and squared unit symbols.
- **Music**: MP3/FLAC/WAV, searched under `/Music` (or the whole card if it does not exist). Long-press to
  favourite; album art is shown and a playback bar stays visible on other screens.
- **Camera**: live preview; photos are saved as JPEG under `/DCIM/Clientre5`.

## 7. Web Files

Start **Web Files** and open `http://<IPv4>/` in a browser (a QR code is shown) to upload, download and edit files.
Uploading many large files over WiFi can be unreliable.

## 8. Appearance, gestures and security

- **Fonts follow the theme and language.** Orange EL / Retro CRT / Pixel render English and Japanese in
  MaruMinya at integer pixel scales. Chinese uses Noto CJK throughout, including Latin text, so faces do not
  mix. Dark / Light / Nord / Solarized / Gruvbox / Classic switch to smooth Montserrat or Noto CJK, and the
  terminal uses a smooth monospace CJK face.
- **Appearance** (Settings → Display → Appearance): Orange EL is the default (black, amber text, square
  corners, glow, scan lines). Also Retro CRT (green phosphor), Pixel, Dark, Light, Nord, Solarized Dark,
  Gruvbox and Classic. You can change all eight colours, corner radius, padding density, glow, scan lines,
  tile colours, terminal foreground/background/cursor, the 16 ANSI colours, cursor shape, blinking and line height.
- **Status bar** (Settings → Status bar): toggle WiFi / Tailscale icons, IP, battery, time, date and more.
  Icons blink while connecting and turn grey when disconnected.
- **Gestures**: **swipe right to go back** on any screen. **Long-press** a home tile for a shortcut
  (SSH = add server, Files = USB stick, Music = play/pause, WiFi = disconnect, Tailscale = on/off,
  Settings = appearance). Swipe left/right in music and images to change track/image. Long-press in the
  terminal for a menu.
- **Security** (Settings → Security): the SSH server list can be stored encrypted on the TF card. The key
  stays in the device, so neither the card nor the device alone can decrypt it. Without the card the list is
  empty; it comes back when the card is reinserted.

## 9. USB

- **USB disk mode**: exposes the TF card to a PC as USB storage. **Connect USB-C to the PC.** While active,
  the device cannot access the card, and because USB-C is switched away from USB serial, **the serial console
  and flashing are unavailable** (press Stop on screen or power-cycle to restore). USB-A host mode uses a
  separate controller, so USB keyboards keep working.
- **USB sticks**: a stick in the USB-A port is mounted at `/usb` and available in Files. FAT32 formatting is supported.

## 10. Firmware updates

On the **Firmware** screen: check for updates → download to the TF card → install. Installing reboots into the
updater (factory partition), which writes the new firmware.
If the device was started from Launcher (no updater present), update through Launcher.

## 11. Remote SSH over Tailscale

Join your tailnet from the **Tailscale** tile on the home screen (uses the bundled unofficial MicroLink client).

1. Create an auth key at `login.tailscale.com/admin/settings/keys` (reusable + pre-authorized is easiest).
2. Enter it on screen, or save it as `/tailscale/authkey.txt` on the TF card and tap **Load key from SD**.
3. Turn on **Join tailnet**. The peer list appears once connected; tap a peer to add it as an SSH server.
   Tailnet names (`nas`, `nas.tailxxxx.ts.net`) work as server hosts.
4. For Headscale, set **Control server** to your server URL.

Node keys are stored on the device, so the auth key is needed only once.

By default all peers go through DERP relays (direct paths can black-hole data behind strict NAT). Expect
~50–150 ms extra latency; the first connection takes 5–30 s while the DERP region switches and the handshake
completes. Turn on **Direct connections (experimental)** to try direct UDP.

For Tailscale SSH targets (`SSH-2.0-Tailscale`), authentication uses the tailnet identity, so leave the
password empty. The default ACL rule `"action": "check"` needs a browser confirmation the Tab5 cannot do:
tag the Tab5 `tag:tab5` and the target `tag:server`, and add
`"action": "accept", "src": ["tag:tab5"], "dst": ["tag:server"]` (when the source is a tag, `dst` cannot be a
user or `autogroup:member`). See [firmware/README.md](../firmware/README.md#tailscale-tailnet-ssh) for a full example.

## 12. Other

- Three-finger tap saves a screenshot to `/ScreenShots`.
- Screensaver: a large clock, monthly calendar, battery and WiFi cards, side by side in landscape and stacked in
  portrait. Set the timeout and a wallpaper folder (JPEG); touch or press any key to return. The legacy
  free-layout mode keeps the older movable widgets and colour settings.
- Backup: export or import server and WiFi settings to `/backup`, encrypted (password) or plain.

# ScreenFuse

Overlays one PC's screen over another PC's screen, across the network, with no
capture card and no video mixer.

One machine - the **remote PC** - has the picture you want underneath. The other

- the **local PC**, where you run ScreenFuse - has a display showing the layer
you want on top: graphics on a black field, drawn by whatever produces them.
ScreenFuse captures both and merges them into one picture, in either of two ways:

- **Screenshots** (`c`, Enter, or Ctrl+Shift+P): both machines capture their
  displays at the same named instant, the two frames are checked against that
  instant and then merged into a single PNG. Precise, verified, one picture at a
  time.

- **Streaming** (`--stream`): the remote PC sends frames continuously and they
  are merged live into a window that OBS, Discord, or anything else that can
  capture a window will take as a source.

The two are different trades, not different quality settings. A screenshot picks
an instant in advance and proves both machines hit it. A stream cannot stop to
negotiate anything, so it pairs each arriving frame with the overlay frame
captured at the same moment and shows the result.

## What it is for

Anything where the thing generating the overlay is not on the same machine as
the thing being overlaid, and you would rather not buy a capture card:

- **Two-PC streaming.** Scoreboards, lower thirds, captions, alerts or
  annotations rendered on a second machine, composited onto the first machine's
  output for OBS, Discord or a meeting client. The second machine's load never
  touches the machine doing the real work.
- **Instrumentation and telemetry.** A dashboard, monitoring HUD or debug
  visualiser rendered beside the system it describes, and shown on top of it.
- **Cross-machine QA.** Two displays that are supposed to agree, captured at the
  same instant with proof of how close that instant was, then diffed or archived
  with a machine-readable sidecar.
- **Any hardware luma keyer you would rather not own.** That is all the
  compositor is: black in the overlay lets the picture underneath through.

## Two halves

| Executable | Runs on | What it does |
| --- | --- | --- |
| `screenfuse_agent.exe` | the remote PC | Listens. Captures its display at an instant it is told, sends the pixels back. Nothing else. |
| `screenfuse.exe` | the local PC | Drives everything: schedules the instant, captures the overlay monitor here, validates, merges, saves. |

The agent initiates nothing, touches nothing else on the machine it runs on -
no injection, no hooks, no reading other processes - and exits when the
controller disconnects. It duplicates a desktop and talks over one TCP socket,
which is what every screen-sharing application does.

## Requirements

- Windows 10 or 11, x64, on both machines.
- A GPU and driver supporting D3D11 and DXGI 1.2 - everything since about 2012.
  Where Desktop Duplication is unavailable, GDI is used instead.
- To build: Visual Studio 2022 (or Build Tools) with the **Desktop development
  with C++** workload. Nothing else - every dependency ships with Windows.

## Setup

```cmd
build.bat                                   # produces build\screenfuse.exe and build\screenfuse_agent.exe
build\screenfuse.exe --genkey               # writes build\screenfuse.key
```

Copy `screenfuse_agent.exe` **and** `screenfuse.key` to the remote PC. Both sides
need the same key file - it is what stops anything else on the network from
asking that machine for a picture of its screen.

On the remote PC:

```cmd
screenfuse_agent.exe --allow 192.168.1.50
```

`--allow` is the local PC's address. Without it, any host holding the key may
connect. If inbound connections are blocked, allow TCP 20787 (or whatever
`--port` you pass on both sides): *Windows Defender Firewall → Advanced Settings
→ Inbound Rules → New Rule → Port → TCP → 20787 → Allow*.

On the local PC:

```cmd
screenfuse.exe --host 192.168.1.20
```

Then `c` or Enter (or **Ctrl+Shift+P** from anywhere) takes a screenshot, `q`
quits and shuts the agent down with it.

Both executables have to come from the same build. The wire protocol carries a
version and a mismatch is refused with a message saying so, rather than failing
in some more interesting way later - so take both from the same release.

### Which monitor is the overlay

By default the primary display is used, and the choice is printed at startup.
On a multi-monitor machine, either name the index:

```cmd
screenfuse.exe --host 192.168.1.20 --overlay-monitor 1
```

or have ScreenFuse find it by the window that is drawing on it, which survives
monitors being rearranged:

```cmd
screenfuse.exe --host 192.168.1.20 --overlay-window "My Overlay"
```

`--overlay-window` matches window classes first, then titles. `--list` prints
this PC's monitors and marks the one that window is on.

## Security

The key authenticates the request. It does **not** encrypt the pixels - the
image crosses the network in the clear, and so does the fact that a screenshot
was taken. That is fine on a home or lab LAN and is not fine anywhere else. To
cross an untrusted network, put both machines on a WireGuard or Tailscale
interface and point `--host` at the tunnel address.

Treat `screenfuse.key` as a password: anyone with it and a route to the agent can
ask that machine for its screen. It is generated with the system RNG, and
`--genkey` refuses to overwrite an existing one.

## Streaming

```cmd
screenfuse.exe --host 192.168.1.20 --stream
```

That opens a window called **ScreenFuse Output** containing the merged picture,
live. In OBS: *Sources → Window Capture*. In Discord: *Share Your Screen →
Applications → ScreenFuse Output*. The window can be moved, resized or left
behind another window - it is captured from its own buffer, not from what is
visible on screen.

While streaming, `s` or Enter (or Ctrl+Shift+P) saves the frame currently on
screen as a PNG, so screenshots are still available without stopping.

| Option | Default | What it changes |
| --- | --- | --- |
| `--stream-fps <n>` | 30 | Frames a second, captured and shown |
| `--stream-quality <n>` | 80 | JPEG quality the remote PC encodes with |
| `--stream-scale <n>` | 100 | Percent of the remote PC's resolution to send |
| `--stream-raw` | off | Uncompressed frames - wired gigabit only |
| `--stream-monitor <n>` | window | Show the output fullscreen on a monitor here |
| `--stream-title <text>` | ScreenFuse Output | Title of the output window |

`--overlay-delay`, `--merge`, `--key-low` and `--key-high` mean the same things
they do for screenshots.

The output window opens at the merged frame's own resolution, because whatever
captures it gets the size of the window's client area, not the size of the
buffer behind it - a half-size window is half the resolution thrown away before
the capture ever sees it, and the first thing that costs is single-pixel overlay
lines. If the frame is bigger than this display, the window shrinks to fit and
says so; `--stream-monitor <n>` shows it full size on a dedicated display
instead.

Bandwidth is the thing to tune first. At 1080p, JPEG quality 80 is roughly
130 Mbit/s at 60 fps and 65 Mbit/s at 30 - fine on wired gigabit, too much for
Wi-Fi. Drop `--stream-fps` first, then `--stream-scale`, then `--stream-quality`;
scale costs less picture than quality does. Measured end to end on a wired
setup, a frame is about 20 ms old by the time it is on screen here, before a
downstream encoder adds its share.

If something in the chain wants a webcam rather than a window, point OBS at the
window and turn on OBS's virtual camera; nothing here needs to change for that.

`--stream-monitor <n>` puts the output fullscreen on a display instead, for
capturing a whole monitor. Do not point it at the monitor the overlay is on:
that would feed the merged picture back into its own input.

Audio is not touched.

## Merging

The default is a luma key, which is what a hardware keyer does: black in the
overlay lets the picture underneath through.

Two thresholds bound it, and they do different jobs:

- **`--key-low` (8)** is the cutoff. Overlay pixels at or below this luma vanish
  completely. **This is the knob to reach for when the overlay's black is not
  quite black** and its background is washing over or blacking out the picture
  underneath. Raise it until the background disappears.
- **`--key-high` (40)** is the top of a soft ramp whose only job is to keep
  anti-aliased overlay edges from coming out jagged.

Everything *between* the two is faded in proportion, so a wide gap is not a
gentler key - it is a dimmer overlay. At `--key-low 8 --key-high 80`, an overlay
pixel of luma 48 comes out at 55% opacity where at `--key-high 40` it was solid,
while anything already bright (a saturated line at luma 193, white at 255) is
unaffected either way. Widening the gap therefore does nothing at all to the
bright parts of an overlay and quietly washes out the dim ones: labels, thin
lines, small text, faint edges.

So: **raise `--key-low` to kill a not-quite-black background, and keep
`--key-high` close to it.** ScreenFuse prints what the current pair actually does
to each captured overlay frame, which turns this from guesswork into a number:

```cmd
INFO   PASS key --key-low 8 --key-high 40: 97.2% of the overlay drops out,
            2.7% comes through solid, 0.1% is faded
```

A large faded percentage means the band is too wide for the content. A large
drop-out percentage with nothing solid means the overlay is darker than
`--key-low`.

`--merge screen` keeps every overlay pixel instead, including dark ones a luma
key would drop, at the cost of brightening the frame underneath.

## Output

```cmd
Captured\20260812-201745-307_merged.png              <- the fused screenshot
Captured\sources\20260812-201745-307_remote.png      <- the remote PC's frame
Captured\sources\20260812-201745-307_overlay.png     <- the overlay frame
Captured\sources\20260812-201745-307_capture.json    <- timings, monitors, every check
Captured\rejected\...                                <- same three files when validation failed
```

Sources are kept so a capture can be merged again with different key settings
without going back to the source:

```cmd
screenfuse.exe --remerge-remote  Captured\sources\..._remote.png ^
               --remerge-overlay Captured\sources\..._overlay.png ^
               --key-low 32 --key-high 48
```

That path does no capture and no network, so it is the fast way to find the
thresholds a given overlay wants.

## How the two machines end up on the same instant

1. **Clock offset.** On connect, 15 SNTP-style round trips over the same TCP
   link. The sample with the shortest round trip wins, because that is the one
   least distorted by queueing. On wired gigabit this lands inside a millisecond
   - far below one frame either side.
2. **A named instant.** The controller picks `now + --lead` (250 ms by default),
   converts it into the agent's timeline, and sends it. Both sides then wait for
   that instant and capture the frame that is on their screen when it arrives.
3. **Proof, not assumption.** Both halves report when they actually captured and
   when the returned frame was presented (Desktop Duplication hands back a
   presentation timestamp). The controller prints how far each side landed from
   the instant it was given, and how far apart the two captures were. A capture
   that misses by more than `--max-skew` (33 ms) is rejected, not saved.

Typical result on a wired LAN: **the two captures land under a millisecond
apart.**

### Frame age is not misalignment

A frame presented 200 ms ago is still exactly what was on screen at the agreed
instant, if nothing changed in between. Age is reported, and warned about past
`--max-age` (250 ms) because during live content it means that display had
stopped updating - but it is not a rejection on its own.

### When the overlay lags what it describes

An overlay derived from a separate data source or render pass trails the picture
it belongs with by however long that pipeline takes. At instant T the remote
display shows the world at T, while the overlay may still be describing the
world as it was tens of milliseconds earlier. A hardware mixer has the same
problem and cannot do anything about it. Here you can:

```cmd
screenfuse.exe --host 192.168.1.20 --overlay-delay 30
```

which captures the overlay 30 ms *after* the base frame, so the two line up.
Start at 0, raise it until it looks right, then leave it.

## Staying connected

An idle connection is a working connection. The controller may sit for an hour
between screenshots, and the agent does not treat that silence as a disconnect.

Three things keep it that way:

- The receive timeout bounds how long a single *transfer* may stall, not how
  long the link may sit quiet.
- TCP keepalive probes start after 5 seconds of silence, so a machine that was
  switched off, unplugged or crashed is noticed in seconds instead of never.
- The controller pings every 5 seconds while idle and rebuilds the link the
  moment it stops answering. A capture that still lands on a broken link is
  retried once after reconnecting rather than being lost.

The same ping re-measures the clock offset, which matters over a long session:
two PCs' clocks drift apart by about a millisecond every few minutes, and the
skew budget a capture is judged against is 33.

A stream reconnects on its own too - the output window stays where it is, and
frames resume when the agent comes back.

## Validation before anything is saved

Every capture is checked, and the results go into the console and the JSON
sidecar:

- the remote frame arrived complete, and decodes to the geometry it claimed
- neither frame is a single flat colour (a dead capture)
- both machines captured within `--max-skew` of the instant they were given, and
  within `--max-skew` of each other after `--overlay-delay` is accounted for
- no frame was presented *after* its own capture instant
- what the current `--key-low` / `--key-high` pair does to the overlay frame
- both frames are the same resolution, or the overlay gets scaled

`--min-black <0..1>` adds one more: refuse the capture unless the overlay frame
is at least that black. It is off by default, because plenty of things worth
compositing are not mostly black - but if yours is, `--min-black 0.5` is a cheap
way to catch the wrong monitor before a session's worth of screenshots go into
the rejected folder.

## Wire format

Screenshots go raw BGRA by default - about 9 MB for 1920x1200, roughly 70 ms on
gigabit, and no encoding work on the remote PC at all. `--png` makes the agent
encode instead (~240 KB for the same frame, so ~38x less network) at the cost of
~100 ms of CPU there. Use `--png` on Wi-Fi, raw on wired.

Streaming defaults to JPEG for the same reason inverted: raw is 8 MB a frame,
which is about 14 fps of gigabit before anything else on the network gets a look
in. `--stream-raw` exists for a quiet wired link where CPU on the remote PC
matters more than bandwidth.

## Limits worth knowing

- **Exclusive fullscreen.** Desktop Duplication returns nothing under true
  exclusive fullscreen. Borderless and fullscreen-optimised windowed modes - the
  default for most modern applications - are fine. If duplication is unavailable
  the agent falls back to GDI, which is slower, has no presentation timestamp,
  and says so in the checks rather than pretending the capture is as good.
- **The image crosses the network in the clear.** See [Security](#security).
- **Something does run on the remote PC.** It performs no injection, no hooking
  and no reads of any other process - it duplicates the desktop and talks over
  one socket. It is still a process on that machine, which is worth knowing if
  that machine is one you do not fully control.
- Secure desktops (UAC prompt, lock screen) cannot be captured. The agent
  reports access denied instead of silently returning black.
- **A stream is not a verified capture.** Frames are paired after the fact by
  the timestamps they carry, and the pairing is reported (`overlay paired
  +2.5 ms from the remote frame` in the status line) but nothing is rejected -
  showing a slightly mismatched frame beats showing nothing. When it has to be
  right, take a screenshot: that path still schedules an instant and refuses
  anything that misses it.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `cannot connect to <host>:20787` | The agent is not running, or inbound TCP 20787 is blocked. See [Setup](#setup). |
| `bad frame magic - the peer is not ScreenFuse` | Something else is on that port. |
| `protocol version mismatch` | The two executables are from different builds. Take both from the same release. |
| `AUTH FAILED - wrong key` | The two `screenfuse.key` files differ. Copy one over the other. |
| `no window matches --overlay-window "..."` | The overlay application is not running, or its class and title differ from the name given. `--list` shows what is where; `--overlay-monitor <n>` skips the search. |
| The overlay is washed out or half missing | The key band is too wide, or the overlay is darker than `--key-low`. Read the `key --key-low ... --key-high ...` line the checks print. See [Merging](#merging). |
| Captures rejected on skew | Something is stalling one machine at capture time. Raise `--lead`, and check neither side fell back to GDI. |
| `desktop duplication is already in use` | Another application holds the maximum number of duplications on that output. |
| `access denied - a secure desktop is up` | A UAC prompt or the lock screen is showing on the captured machine. |

## Files

```cmd
src\common.hpp          time, hex, keys, HMAC, logging
src\net.hpp             wire protocol, framing, auth, clock sync, keepalive
src\capture.hpp         monitor enumeration, Desktop Duplication, GDI fallback,
                        continuous capture for streaming
src\image.hpp           image container, scaling, luma-key merge, PNG and JPEG via WIC
src\stream.hpp          threaded frame handoff, parallel compositing, output window
src\agent_main.cpp      remote PC half
src\controller_main.cpp local PC half
tools\make_test_frames.cpp  synthetic frames for checking the compositor
```

Both executables are one translation unit each, no external dependencies -
everything comes from the OS (DXGI, D3D11, WIC, BCrypt, Winsock).

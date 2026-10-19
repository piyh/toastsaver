# toastsaver

[![CI](https://github.com/piyh/toastsaver/actions/workflows/ci.yml/badge.svg)](https://github.com/piyh/toastsaver/actions/workflows/ci.yml)

Retro screensavers for Raspberry Pi OS and other Wayland desktops: flying toasters, a rainbow waffle cat, 3D pipes, a cozy bookshelf, birds in a summer sky and more, with a big friendly picker that a six-year-old can use.

![toastsaver demo](docs/demo.gif)

```sh
curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh
```

## What's inside

Twelve screensavers, plus a SURPRISE ME button that picks a random one each time from the ones you put in the mix. All of the artwork is original and drawn by the program itself.

<table>
  <tr>
    <td align="center"><img src="docs/toasters.png" width="400"><br><b>TOASTERS</b></td>
    <td align="center"><img src="docs/rainbow.png" width="400"><br><b>RAINBOW</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/pipes.png" width="400"><br><b>PIPES</b></td>
    <td align="center"><img src="docs/books.png" width="400"><br><b>BOOKS</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/dice.png" width="400"><br><b>DICE</b></td>
    <td align="center"><img src="docs/aquarium.png" width="400"><br><b>FISH</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/birds.png" width="400"><br><b>BIRDS</b></td>
    <td align="center"><img src="docs/starfield.png" width="400"><br><b>STARS</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/mystify.png" width="400"><br><b>MYSTIFY</b></td>
    <td align="center"><img src="docs/matrix.png" width="400"><br><b>MATRIX</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/lava.png" width="400"><br><b>LAVA</b></td>
    <td align="center"><img src="docs/name.png" width="400"><br><b>NAME</b></td>
  </tr>
</table>

- **TOASTERS**: winged chrome toasters and slices of toast flying down and to the left.
- **RAINBOW**: the Waffle Cat flies across the night sky on a wavy rainbow. Every so often it shimmers, goes *poof*, rainbow lines zoom across the screen, and it pops back out at the end of the rainbow. (Invented by a six-year-old.)
- **PIPES**: shiny 3D pipes grow through a grid, then fade and start again.
- **BOOKS**: a cozy lamplit bookshelf. Books slide out, float to the middle, open to a little animated picture, and fly back to their spot.
- **DICE**: 3D dice (d6, d8, d10, d12 and d20) are thrown together onto a felt tray. Then each number flies up to a chalkboard, slowly enough for a kid to add them up first. A natural 20 on the d20 gets gold rays, confetti and "CRIT!"; a natural 1 gets a grumpy rain cloud.
- **FISH**: clownfish, a blue tang, a pufferfish, seaweed and a bubbling treasure chest.
- **BIRDS**: a sunny summer sky with bluebirds, cardinals, finches and robins flying over a flower meadow. (Also requested by a six-year-old.)
- **STARS**: a warp-speed starfield.
- **MYSTIFY**: bouncing, color-changing shapes with echo trails.
- **MATRIX**: falling green code. Sometimes a column spells your name.
- **LAVA**: a glowing lava lamp with wax that warms, rises, merges and sinks, in a new color each time.
- **NAME**: your name bounces around like the old DVD logo, with confetti when it hits a corner.

### Easter eggs

Keep watching the toasters. Every so often a toaster flies by in a **propeller beanie**, a **golden toast** sparkles past, or a **bagel** sneaks in with the toast.

![A toaster, the propeller beanie, toast, golden toast and a bagel](docs/eggs.png)

## Install

### Raspberry Pi OS, Debian and Ubuntu (64-bit, 32-bit Pi and x86)

```sh
curl -fsSL https://piyh.github.io/toastsaver/setup.sh | sh
```

[`setup.sh`](site/setup.sh) adds the toastsaver APT repository and installs the package. If you'd rather not pipe a script into your shell, these four lines do the same thing:

```sh
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://piyh.github.io/toastsaver/toastsaver.gpg | sudo tee /etc/apt/keyrings/toastsaver.gpg >/dev/null
echo "deb [signed-by=/etc/apt/keyrings/toastsaver.gpg] https://piyh.github.io/toastsaver stable main" | sudo tee /etc/apt/sources.list.d/toastsaver.list
sudo apt update && sudo apt install toastsaver
```

Updates then arrive with your normal system updates. You can also download a `.deb` from the [releases page](https://github.com/piyh/toastsaver/releases) and install it with `sudo apt install ./toastsaver_*.deb`.

#### Automatic updates

To have toastsaver update itself (handy on a kid's computer nobody runs `apt upgrade` on), turn on a once-a-day check. It refreshes only the toastsaver repository and upgrades only toastsaver; nothing else on the system changes:

```sh
curl -fsSL https://raw.githubusercontent.com/piyh/toastsaver/main/extras/enable-auto-updates.sh | sh
```

[`enable-auto-updates.sh`](extras/enable-auto-updates.sh) adds a small systemd timer; the script's first lines say how to undo it.

#### Switching from a source or USB install

If you installed with `./install.sh` or from a USB stick, this removes that install (keeping your settings), installs the apt package, and turns on automatic updates:

```sh
curl -fsSL https://raw.githubusercontent.com/piyh/toastsaver/main/extras/migrate-to-apt.sh | sh -s -- --auto-updates
```

Leave off `-s -- --auto-updates` to skip the automatic updates.

The repository is signed with this key (also in [`packaging/toastsaver-archive-keyring.asc`](packaging/toastsaver-archive-keyring.asc)):

```
01E0 71BE 5AE3 0687 7A69  7699 EDD0 90A6 DE03 BB9F
```

### Arch Linux

A [`PKGBUILD`](packaging/aur/PKGBUILD) is included. Build it with `makepkg -si` from that folder. An AUR package is coming.

### From source

```sh
git clone https://github.com/piyh/toastsaver.git
cd toastsaver
./install.sh
```

This installs the build tools, builds, and installs under `/usr/local`. To install offline from a USB stick, copy the folder onto the stick and run `sh /media/$USER/<stick>/toastsaver/extras/deploy-from-usb.sh` on the Pi.

### After installing

Log out and back in (or run `toastsaver-idle --restart`). The screensaver now starts by itself whenever the computer has been idle for 5 minutes.

## Using it

### The picker (for kids)

Open **Screensavers** from the app menu. To put an icon on the desktop too, run `toastsaver --desktop-icon`.

![The picker](docs/picker.png)

- **Tap a tile** to choose it. Your choice is saved right away. There is no Save button.
- **Tap the chosen tile again**, or the big green **TRY IT!** button, to watch it.
- **Move the mouse, touch the screen, or press any key** to go back to the picker.
- **SURPRISE ME!** (under the tiles) shows a different saver each time. While it is on, the tiles show which savers are **in the mix**: tap a tile to put it in (green check) or take it out (dimmed). At least one always stays in. Tap **SURPRISE ME!** again to turn it off and choose a single tile.
- **NAME**: when it is chosen, type your name on the keyboard.
- The **big red X** closes the picker.

On a keyboard, use the arrow keys to move, **Enter** to try, **Tab** to turn SURPRISE ME on or off, **Space** to put the highlighted saver in or out of the mix, and **Esc** to close.

### Screensaver Settings (for grown-ups)

Open **Screensaver Settings** from the app menu. Changes are saved right away, and new timings take effect as soon as you close the window.

![Screensaver Settings](docs/settings.png)

- **Screensaver**: turn it on or off.
- **Chosen**: the current screensaver, or **SURPRISE**. **Change** opens the picker, where you also pick the surprise mix.
- **Start after**: how long the computer waits before showing the screensaver.
- **Screen off after**: when the display turns off, or **NEVER**. It is always later than the screensaver.
- **Show clock**: puts the time over any screensaver. It drifts to a new spot about once a minute, so it never sits in one place.
- **Clock style**: 12-hour or 24-hour.
- **Test it** shows the current screensaver.

![The clock over the aquarium](docs/clock.png)

### Commands

| Command | What it does |
| --- | --- |
| `toastsaver-picker` | Open the picker |
| `toastsaver-settings` | Open Screensaver Settings |
| `toastsaver-idle --now` | Show your chosen saver now |
| `toastsaver-idle --now pipes` | Show a specific saver now |
| `toastsaver-idle --check` | Check that everything is set up and running |
| `toastsaver-idle --restart` | Apply new settings now |
| `toastsaver --set CLOCK=1` | Change any setting from a terminal |
| `toastsaver --list` | List the saver names |
| `toastsaver --desktop-icon` | Put a Screensavers icon on the desktop |
| `toastsaver --windowed toasters` | Run a saver in a window |

See `man toastsaver` for everything.

### Settings file

Settings live in `~/.config/toastsaver.conf`. The picker and Screensaver Settings edit it for you and keep your comments.

```sh
ENABLED=1               # 0 = turn the screensaver off
SAVER=toasters          # toasters rainbow pipes books dice aquarium birds starfield mystify matrix lava name random
RANDOM_SAVERS=          # savers SAVER=random picks from, e.g. toasters,pipes,lava (empty = all)
NAME='HELLO'            # letters, digits, space and ! ? - . *  (* draws a heart)
IDLE_MINUTES=5          # minutes of no input before the screensaver appears
SCREEN_OFF_MINUTES=30   # minutes before the display turns off (0 = never)
CLOCK=0                 # 1 = show a drifting clock over the screensaver
CLOCK_24H=0             # 1 = 24-hour clock
```

## How it works

At login, every user's session starts `toastsaver-idle` from `/etc/xdg/autostart`. It runs [`swayidle`](https://github.com/swaywm/swayidle), which watches for inactivity:

- After `IDLE_MINUTES` with no input, the chosen saver starts full screen.
- Any key, click, touch or mouse movement makes it go away.
- After `SCREEN_OFF_MINUTES`, the display turns off with `wlopm`, if it is installed.

toastsaver never stops the screen from blanking. Apps that block idle the standard Wayland way (the idle-inhibit protocol, which video players and games use while playing) keep the saver from starting.

It is not a lock screen.

## Supported desktops

| Desktop | Status |
| --- | --- |
| labwc (Raspberry Pi OS default) | Tested |
| sway | Tested |
| Wayfire (older Raspberry Pi OS) | Should work, untested |
| Hyprland, river and other wlroots desktops | Should work, untested |
| GNOME, KDE Plasma | Not supported (they use their own idle systems) |
| X11 sessions | Not supported (toastsaver quietly does nothing) |

Tested on a **Raspberry Pi 4 (4 GB) with 64-bit Raspberry Pi OS**, and in Debian trixie and bookworm containers on every change. Only the first monitor is used, and the 3D pipes run best on a Raspberry Pi 4 or 5.

## Uninstall

```sh
sudo apt remove toastsaver       # keeps the autostart file in /etc (it is ignored)
sudo apt purge toastsaver        # removes everything except your ~/.config/toastsaver.conf
```

For a source install, run `./uninstall.sh`.

## Development

You need SDL2 (2.0.18 or newer) and `pkg-config`. Then run `make`. On macOS, `brew install sdl2` and run savers with `--windowed`.

Render a frame without a display:

```sh
toastsaver --snapshot out.png --size 1280x720 --seconds 5 pipes
```

### Tests

[`tests/pi-os-test.sh`](tests/pi-os-test.sh) runs in a throwaway Debian container (the base of Raspberry Pi OS). It installs toastsaver, from source or from a `.deb`, starts a headless compositor (labwc on trixie, sway on bookworm), and checks that:

1. a saver appears after the idle timeout and leaves on a key press,
2. an app that inhibits idle (like a playing video) stops it from starting, and it starts again once that app closes,
3. dismissing a saver that pops up over the picker leaves the picker open,
4. `toastsaver-idle --now pipes` starts that saver,
5. settings: `--set` keeps the file's comments, `--restart` switches to the new timings, `ENABLED=0` stops the watcher, the settings window opens and a saver runs with the clock on,
6. for the `.deb`: `apt remove` and `apt purge` clean up.

```sh
docker run --rm -v "$PWD":/src debian:trixie sh /src/tests/pi-os-test.sh
sh packaging/build-deb.sh arm64          # builds dist/toastsaver_<version>_arm64.deb (also armhf, amd64)
sh packaging/test-debs.sh                # runs the suite against the built packages
```

### Releases

Pushing a tag like `v1.0.1` (matching the top entry in `debian/changelog`) makes GitHub Actions build the three packages, run the tests against them, create a GitHub release, and publish the signed APT repository to GitHub Pages.

## About

Made by a parent for a six-year-old who loves screensavers.

toastsaver was written with the help of an AI assistant (Claude). It is tested on real hardware and by an automated end-to-end suite, and every line is in this repository to read. Bug reports and pull requests are welcome.

## License

MIT. See [`LICENSE`](LICENSE).

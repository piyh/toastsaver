# toastsaver

Retro screensavers for Raspberry Pi OS, with a big, kid-friendly picker to choose your favorite.

![The picker](docs/picker.png)

## What's inside

Eight screensavers, plus a SURPRISE tile that picks a random one each time.

<table>
  <tr>
    <td align="center"><img src="docs/toasters.png" width="400"><br><b>TOASTERS</b></td>
    <td align="center"><img src="docs/pipes.png" width="400"><br><b>PIPES</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/starfield.png" width="400"><br><b>STARS</b></td>
    <td align="center"><img src="docs/mystify.png" width="400"><br><b>MYSTIFY</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/matrix.png" width="400"><br><b>MATRIX</b></td>
    <td align="center"><img src="docs/aquarium.png" width="400"><br><b>FISH</b></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/lava.png" width="400"><br><b>LAVA</b></td>
    <td align="center"><img src="docs/name.png" width="400"><br><b>NAME</b></td>
  </tr>
</table>

- **TOASTERS**: winged chrome toasters and slices of toast flying down and to the left.
- **PIPES**: shiny 3D pipes grow through a grid, then fade and start again.
- **STARS**: a warp-speed starfield.
- **MYSTIFY**: bouncing, color-changing shapes with echo trails.
- **MATRIX**: falling green code. Sometimes a column spells your name.
- **FISH**: clownfish, a blue tang, a pufferfish, seaweed, and a bubbling treasure chest.
- **LAVA**: a lava lamp, with a new random color palette each time.
- **NAME**: your name bounces around like the old DVD logo, with confetti when it hits a corner.

All of the artwork is original and drawn by the program itself. There are no image files.

## Install on a Raspberry Pi

### From a USB stick (easiest)

1. On another computer, copy the `toastsaver` folder onto a USB stick.
2. Plug the stick into the Raspberry Pi.
3. Open the Terminal and run this, as your normal user (not as root):

   ```sh
   sh /media/$USER/*/toastsaver/deploy.sh
   ```

4. Type your password when asked. It only asks once.

The script copies the folder to `~/toastsaver`, installs the packages it needs, builds the program, installs it, sets it to start at login, adds a **Screensavers** icon, and starts it. To update later, run the same command again.

### From git

```sh
git clone https://github.com/piyh/toastsaver.git
cd toastsaver
./install.sh
```

## Using it

### The picker

Open **Screensavers** from the desktop icon or the app menu. The picker fills the screen with nine big, live tiles.

- **Tap a tile** to choose it. Your choice is saved right away. There is no Save button.
- **Tap the chosen tile again**, or the big green **TRY IT!** button, to watch it.
- **Move the mouse, touch the screen, or press any key** to go back to the picker.
- **SURPRISE** picks a random saver each time.
- **NAME**: when it is chosen, type your name on the keyboard.
- **Big red X** closes the picker.

On a keyboard, use the arrow keys to move, **Enter** to try, and **Esc** to close.

### Commands

| Command | What it does |
| --- | --- |
| `toastsaver-picker` | Open the picker |
| `toastsaver-idle --now` | Show your chosen saver now |
| `toastsaver-idle --now pipes` | Show a specific saver now |
| `toastsaver-idle --check` | Check that everything is set up and running |
| `toastsaver --list` | List the saver names |
| `toastsaver --windowed toasters` | Run a saver in a window (for testing) |
| `./uninstall.sh` | Remove toastsaver (your settings are kept) |

## Settings

Settings live in `~/.config/toastsaver.conf`. The file uses simple shell syntax:

```sh
IDLE_MINUTES=5          # minutes with no input before the saver starts
SCREEN_OFF_MINUTES=30   # minutes before the screen turns off (0 = never)
SAVER=toasters          # any saver name from the list above, or random
NAME='HELLO'            # letters, digits, space and ! ? - . *  (shown in capitals)
```

In `NAME`, the `*` character draws a heart.

## How it works

When you log in, `toastsaver-idle` starts automatically (through `~/.config/autostart`) and runs `swayidle`, which watches for inactivity.

- After `IDLE_MINUTES` with no input, the saver you chose starts full screen.
- Any key, click, touch, or mouse movement makes it go away.
- After `SCREEN_OFF_MINUTES`, the display turns off using `wlopm`, if it is installed.

toastsaver does not change the Pi's own screen blanking settings, and it never stops the screen from blanking.

Apps that block idle the standard Wayland way (the idle-inhibit protocol, which video players and games use while playing) keep the saver from starting. This is covered by the tests below.

## Development

You need SDL2 (version 2.0.18 or newer) and `pkg-config`. Then run `make`.

On macOS, you can develop with a window instead: install SDL2 with `brew install sdl2`, then build and run a saver with `--windowed`.

To save a picture of a saver without a display, use:

```sh
toastsaver --snapshot out.png --size 1280x720 --seconds 5 pipes
```

### Tests

The tests run in a throwaway Debian ARM64 Docker container (the base of Raspberry Pi OS). The container builds the program, runs the real installer, starts a headless compositor, and checks that:

1. a saver appears after the idle timeout and leaves on a key press,
2. an app that inhibits idle (like a playing video) stops it from starting, and it starts again once that app closes,
3. dismissing a saver that pops up over the picker leaves the picker open,
4. `toastsaver-idle --now pipes` starts that saver.

```sh
docker run --rm -v "$PWD":/src debian:trixie sh /src/tests/pi-os-test.sh
```

Trixie uses labwc. Bookworm uses sway.

## Known limits

- Works on Wayland only. Tested with labwc (Raspberry Pi OS's default) and sway; Wayfire should work but is untested. It does not work on X11.
- So far tested in containers, not yet on real Raspberry Pi hardware.
- It is not a lock screen. It does not ask for a password.
- It only uses the first monitor.
- The 3D pipes run best on a Raspberry Pi 4 or 5.

## License

MIT. See the `LICENSE` file.

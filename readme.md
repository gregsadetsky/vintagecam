# vintagecam

userspace libusb drivers for pre-uvc usb webcams on macos. one binary, picks
the driver by usb vid:pid, writes finished frames to stdout.

```sh
brew install libusb ffmpeg
make
make view                          # live preview in ffplay
make view ARGS="--freq 60"          # 60 hz mains flicker table
./vintagecam --list                # supported cameras, connected or not
./vintagecam --frames 100 > out.mjpeg
```

flags: `--width 640|320` (default 640), `--freq 0|50|60` (mains flicker table,
0 = none), `--frames N`, `--seconds S`, `--alt N`, `--dump-packets N`,
`-v` (stats on stderr), `--reg HEX=HEX` / `--i2c HEX=HEX` (override a value
the init table writes to a bridge / sensor register, for experiments).

## cameras

| camera | usb id | chips | status |
| --- | --- | --- | --- |
| creative vf0250 | 041e:4051 | zc3xx bridge + gc0303 sensor | works at 640x480 ~14 fps mjpeg. 320x240 (~28 fps) streams but has almost no color |

## adding a camera

the engine is shaped like linux gspca (`init`, `start`, `poll`, `stop`,
`pkt_scan`, `frame_add` with FIRST/INTER/LAST/DISCARD), so a gspca subdriver
ports over mostly by copying. add a `struct cam_driver` and list it in
`drivers[]` in main.c. `--dump-packets` shows raw iso packets for checking
frame headers against what the kernel source says.

## notes

- vf0250 at 320x240: picture is nearly monochrome with a faint tint that drifts
  between green and magenta from run to run. cause unexplained. tried without
  success: bridge regs 0098/009a/011a/011c = 1, 0002 = 11/12/14/18/30/50,
  sensor regs 0a/0c/0e/10 = 0 or 2.
- macos leaves these devices unconfigured, so the engine sets configuration 1
  before claiming the interface.
- `gspca/` holds headers and tables copied from the linux kernel, hence gpl.
- not done yet: syphon output, snapshot button (interrupt ep 0x82), controls
  for brightness/contrast/gamma/sharpness.

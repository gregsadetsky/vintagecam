# vintagecam

userspace libusb drivers for pre-uvc usb webcams on macos. one binary, picks
the driver by usb vid:pid, writes finished frames to stdout.

```sh
brew install libusb ffmpeg
make
make view                          # live preview in ffplay
make view ARGS="--width 640 --freq 60"
./vintagecam --list                # supported cameras, connected or not
./vintagecam --frames 100 > out.mjpeg
```

flags: `--width 320|640`, `--freq 0|50|60` (mains flicker table, 0 = none),
`--frames N`, `--alt N`, `--dump-packets N`, `-v` (stats on stderr).

## cameras

| camera | usb id | chips | status |
| --- | --- | --- | --- |
| creative vf0250 | 041e:4051 | zc3xx bridge + gc0303 sensor | works, mjpeg, 320x240 ~28 fps, 640x480 ~14 fps |

## adding a camera

the engine is shaped like linux gspca (`init`, `start`, `poll`, `stop`,
`pkt_scan`, `frame_add` with FIRST/INTER/LAST/DISCARD), so a gspca subdriver
ports over mostly by copying. add a `struct cam_driver` and list it in
`drivers[]` in main.c. `--dump-packets` shows raw iso packets for checking
frame headers against what the kernel source says.

## notes

- macos leaves these devices unconfigured, so the engine sets configuration 1
  before claiming the interface.
- `gspca/` holds headers and tables copied from the linux kernel, hence gpl.
- not done yet: syphon output, snapshot button (interrupt ep 0x82), controls
  for brightness/contrast/gamma/sharpness.

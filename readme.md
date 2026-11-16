# vintagecam

userspace libusb drivers for pre-uvc usb webcams on macos. one binary, picks
the driver by usb vid:pid, writes finished frames to stdout.

```sh
brew install libusb ffmpeg
make
make view                          # preview window only
make record                        # record to recordings/ with a preview window
make record ARGS="--width 320"     # 320x240, about twice the frame rate
./vintagecam --list                # connected supported cameras and their usb path
./vintagecam --record DIR --preview --device 2-1.4
```

one process drives one camera. run one per camera, picked with `--device`
(the usb path from `--list`, stable for a given physical port).

- `--record DIR` writes every frame, in the camera's own jpeg bytes, to
  `DIR/<time>-<camera>-usb<path>.mjpeg` plus a `.csv` index (frame number,
  seconds since first frame, unix time, byte offset, size). writing happens on
  its own thread. it never overwrites a file. the summary at the end says how
  many frames were captured vs written and prints an ffmpeg line to make an mkv.
- `--preview` opens an ffplay window fed only the latest frame, so it can lag
  or be closed without touching the recording.
- `--stdout` writes the raw frame stream to a pipe.

other flags: `--width 640|320` (default 640), `--freq 0|50|60` (mains flicker
table, 0 = none), `--frames N`, `--seconds S`, `--alt N` (usb bandwidth
reservation, see notes), `--dump-packets N`, `-v` (stats on stderr),
`--reg HEX=HEX` / `--i2c HEX=HEX` (override a value the init table writes to a
bridge / sensor register, or write an extra one, for experiments).

## cameras

| camera | usb id | chips | status |
| --- | --- | --- | --- |
| creative vf0250 | 041e:4051 | zc3xx bridge + gc0303 sensor | works, mjpeg, 640x480 ~14 fps and 320x240 ~28 fps |

## adding a camera

the engine is shaped like linux gspca (`init`, `start`, `poll`, `stop`,
`pkt_scan`, `frame_add` with FIRST/INTER/LAST/DISCARD), so a gspca subdriver
ports over mostly by copying. add a `struct cam_driver` and list it in
`drivers[]` in main.c. `--dump-packets` shows raw iso packets for checking
frame headers against what the kernel source says.

## notes

- vf0250 at 320x240: with the kernel table as is, green comes out swapped with
  red/blue. the driver writes sensor reg 0x17 = 0xe9 (table says 0xe8) in 320
  mode, which gives the right order on ntsc bars over 5 starts. why one extra
  row fixes it: unexplained. `--i2c 17=e8` brings the old behaviour back.
- usb bandwidth: the default alt setting reserves the most (896 bytes/ms on
  the vf0250). measured on the vf0250, 6s runs: 320x240 keeps its full 28 fps
  down to `--alt 4` (384 bytes/ms) and 27.6 fps at `--alt 3` (256); 640x480
  needs alt 7 for 14 fps (alt 5 gives 11, alt 4 gives 8.5, below that nothing).
  smaller reservations should let more cameras share a bus (untested, one camera so far).
- macos leaves these devices unconfigured, so the engine sets configuration 1
  before claiming the interface.
- `gspca/` holds headers and tables copied from the linux kernel, hence gpl.
- not done yet: syphon output, raw (non jpeg) cameras, exact per frame timing in
  the mkv (the index has it, the ffmpeg line uses the average rate), snapshot button (interrupt ep 0x82), controls
  for brightness/contrast/gamma/sharpness.

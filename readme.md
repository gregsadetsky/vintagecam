# vintagecam

userspace libusb drivers for pre-uvc usb webcams on macos. one binary, picks
the driver by usb vid:pid, writes finished frames to stdout.

```sh
brew install libusb ffmpeg jpeg-turbo
make
make view                          # preview window only
make record                        # record to recordings/ with a preview window
make record ARGS="--width 320"     # 320x240, about twice the frame rate
./vintagecam --list                # connected supported cameras and their usb path
./vintagecam --record DIR --preview --device 2-1.4
```

one process drives one camera. run one per camera, picked with `--device`
(the usb path from `--list`, stable for a given physical port).

- `--record DIR` writes every good frame, in the camera's own jpeg bytes, to
  `DIR/<time>-<camera>-usb<path>.mjpeg`. next to it: `.csv` (frame number,
  seconds since first frame, unix time, byte offset, size, camera frame
  counter), `.events.csv` (faults and recoveries) and `.bad` (up to 200
  rejected frames, for a post mortem). it never overwrites a file.
- `--preview` opens an ffplay window fed only the latest frame, so it can lag
  or be closed without touching the recording.
- `--stdout` writes the raw frame stream to a pipe.

## when things go wrong

every frame is fully decoded (turbojpeg) before it is written. a frame that
does not decode cleanly at the expected size is dropped, never recorded.
the main loop restarts things when there has been no good frame for 2s, 20
bad frames in a row, a usb control or iso error, or the device vanished:
first the stream, then (if that produced nothing) close and reopen the
device, then a usb port reset, and it keeps waiting for the camera to come
back on the same usb port for as long as it takes. the recording file stays
open the whole time, so an outage is a gap in the index, not a broken file.

`--export` turns a recording into constant frame rate video in real time,
with black frames wherever there was a gap over 0.5s:

```sh
./vintagecam --export rec.mjpeg --fps 30 | ffmpeg -f mjpeg -framerate 30 -i - -c copy rec.mkv
```

`--test-faults 8:break,20:stall,32:corrupt,48:reset` triggers faults at those
seconds: `break` writes a register that makes the camera send garbage,
`stall` drops all incoming data, `corrupt` damages incoming data for 4s,
`reset` does a usb port reset under the running stream.

what it cannot catch: a picture that is a valid jpeg but looks wrong (bad
colors, frozen, black).

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
- not done yet: syphon output, raw (non jpeg) cameras, snapshot button (interrupt ep 0x82), controls
  for brightness/contrast/gamma/sharpness.

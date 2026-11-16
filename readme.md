# vintagecam

userspace libusb drivers for pre-uvc usb webcams on macos. one binary, picks
the driver by usb vid:pid, records and previews jpeg frames.

```sh
brew install libusb ffmpeg jpeg-turbo
make
make view                          # preview window only
make record                        # record to recordings/ with a preview window
make record ARGS="--width 320"     # vf0250: 320x240, about twice the frame rate
./vintagecam --list                # connected supported cameras and their usb path
./vintagecam --record DIR --preview --device 2-1.4
```

one process drives one camera. run one per camera, picked with `--device`
(the usb path from `--list`, stable for a given physical port).

- `--record DIR` writes every good frame to
  `DIR/<time>-<camera>-usb<path>.mjpeg`: the camera's own jpeg bytes if it
  sends jpeg, otherwise (raw bayer cameras) demosaiced and encoded here at
  quality 95, 4:4:4. next to it: `.csv` (frame number,
  seconds since first frame, unix time, byte offset, size, camera frame
  counter), `.events.csv` (faults and recoveries) and `.bad` (up to 200
  rejected frames, for a post mortem). it never overwrites a file.
- `--preview` opens an ffplay window fed only the latest frame, so it can lag
  or be closed without touching the recording.
- `--stdout` writes the raw frame stream to a pipe.

## when things go wrong

every jpeg frame is fully decoded (turbojpeg) before it is written. a frame
that does not decode cleanly at the expected size is dropped, never recorded.
raw frames can only be checked for their exact byte count.
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

other flags: `--width N` (see the cameras table, first size is the default),
`--exposure N` / `--gain N` (fixed values instead of auto, quickcam only so
far), `--bayer grbg|rggb|bggr|gbrg` (override the raw layout), `--freq 0|50|60`
(mains flicker table, 0 = none, vf0250 only), `--frames N`, `--seconds S`, `--alt N` (usb bandwidth
reservation, see notes), `--dump-packets N`, `-v` (stats on stderr),
`--reg HEX=HEX` / `--i2c HEX=HEX` (override a value the init table writes to a
bridge / sensor register, or write an extra one, for experiments).

## cameras

| camera | usb id | chips | status |
| --- | --- | --- | --- |
| creative vf0250 | 041e:4051 | zc3xx bridge + gc0303 sensor | works, mjpeg, 640x480 ~14 fps and 320x240 ~28 fps |
| logitech quickcam express | 046d:0840 | stv0600 bridge + pb0100 sensor | works, raw bayer, 320x240 ~10 fps and 352x288 ~8 fps. units with an hdcs sensor: not ported |
| lego camera (quickcam web) | 046d:0850 | stv0610 bridge + vv6410 sensor | works, raw bayer, 356x292 ~8.5 fps |
| creative webcam nx (pd1110) | 041e:401c | zc3xx bridge + pas106 sensor | works, mjpeg, 352x288 ~9 to 20 fps depending on the scene. 176x144 written, not run |
| ibm pc camera | 0545:8080, device version 3.0a | xirlink cit, model 2 | works, raw bayer, 320x240 ~7.5 fps and 352x288 ~4.6 fps. 176x144 and other models: not ported |
| sony eyetoy | 054c:0154 | ov519 | not working: this unit's usb config descriptor reads back as 0xff after byte 20, so macos never sees the video endpoint. cause unexplained |

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
  smaller reservations should let more cameras share a bus (untested: the two
  cameras so far ran together fine, but on different buses).
- quickcam express: the sensor's own auto exposure stayed near white for every
  target tried (why: unexplained), so the driver runs its own: it nudges the
  row integration time, then the gain, toward a mean raw value of 105.
  integration time above about 0xf0 lowers the frame rate, so it stops there.
  no white balance yet, the picture is a little green. its one alt setting
  reserves 1023 bytes/ms, which is most of a full speed bus.
- macos leaves these devices unconfigured, so the engine sets configuration 1
  before claiming the interface.
- `gspca/` holds headers and tables copied from the linux kernel, hence gpl.
- four cameras at once (quickcam, lego, nx, ibm; one recorder each, 60s): every
  frame of every camera recorded. the quickcam and the nx were on the same
  usb bus, both at their largest reservation.
- a recorder that dies without stopping the stream (kill -9, crash) used to
  leave the quickcam unable to start again. the stv06xx driver now switches
  the stream off first thing at init.
- `--test-faults N:abandon` stalls the stream and then does not wait for the
  cancelled usb transfers, which is how the engine once crashed at exit.
- ibm: the start of frame marker is just `00 ff`, which can also be two
  pixels. a marker that would end a frame early is treated as picture data.
- not done yet: syphon output, white balance for raw cameras, keeping the raw
  bayer bytes, snapshot button (interrupt ep 0x82), controls
  for brightness/contrast/gamma/sharpness.

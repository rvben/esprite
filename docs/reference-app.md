# A firmware application behind an HTML front end

Paperplane is an external reference integration: a desk companion for a 400×300
e-paper display. It combines weather pages, a focus timer, grayscale artwork
and hardware features. It keeps its own browser UI while Esprite executes the
same application sources used by the physical device.

## Keep three boundaries clear

1. **Firmware owns behavior.** Page transitions, button handling, the focus
   countdown, drawing and persistent-data formats stay in C++.
2. **The bridge owns transport.** One persistent `runner run` process exchanges
   JSON lines. Serialize complete request/reply transactions, cap payloads,
   enforce deadlines and reap the runner on shutdown. Atomically replace the
   captured PNG so the browser cannot read a half-written image.
3. **The browser owns controls and presentation.** Display the captured PNG;
   translate user actions into button, touch or serial commands. Label injected
   readings and disable hardware features the simulation cannot perform.

The generic [HTML studio](../examples/html-studio/README.md) demonstrates the
process lifecycle, bounded protocol, loopback HTTP and control discovery. A
consumer can use it directly or keep an application-specific front end.

## A useful regression set

| Trigger | Check in the simulator | Check on hardware |
| --- | --- | --- |
| UP / DOWN / OK | Correct page, action and resulting pixels | Physical buttons and orientation |
| Focus expires | Remaining time reaches zero; completion event; final frame | Alarm audibility and panel cadence |
| Invalid forecast | Retain the last valid forecast; render a saved-data state | Wi-Fi loss, reconnection and refresh |
| Interrupted memo write | Recover the previous complete, CRC-valid recording | Microphone quality and speaker playback |
| Sleep and wake | Preserve settings and resume the application | Wake circuit, retained ink and current draw |
| Grayscale → monochrome | Reset the refresh baseline before partial updates | Actual grayscale quality and ghosting |

Audio capture is unavailable in the host-native integration. Memo-journal tests
use synthetic PCM and interrupted storage writes; they do not imply that a
microphone or speaker has been tested.

## Make demonstrations safe to share

Use synthetic weather with a generic label, procedural artwork and fresh,
temporary simulator storage. Do not reuse a device backup, real forecast cache,
recording, network scan or photograph. Disable external weather requests and
personal-data inputs in the demo path. Start from neutral embedded assets so
private content cannot flash onscreen before the first update.

The README's Paperplane image follows this approach. It is a real screenshot
of the external application, not a bundled Esprite target or a promise of
hardware emulation.

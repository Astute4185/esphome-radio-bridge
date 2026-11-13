# TubesZB MGM210 PoE (2023): reversible RTS/CTS spike

## Scope and safety

The known-good configuration is **EFR32 2-wire firmware, ESP32 UART1 TX GPIO4,
RX GPIO36, no hardware flow control, TCP 6638**. Preserve it. This spike adds
an optional Home Assistant select entity which alters **normal-mode** UART
flow control without ESP32 OTA. The Gecko bootloader remains 2-wire with the
same GPIO4/GPIO36 pair, so switching EFR32 firmware can continue to use the
existing flasher.

**GPIO2 and GPIO13 are candidate flow-control traces, not proven signal
assignments.** A silk-screen RTS label can refer to either end of the link.
Do not infer direction from label alone. GPIO2 is also an ESP32 strapping pin.
Do not drive GPIO2 against another active output. With power removed, trace
connections to the EFR32; use a high-impedance logic analyzer or meter to
confirm which signal is driven by the radio. Only set the opt-in boolean once
both connections are verified. No software test can reliably establish output
ownership from a static logic level.

## ESPHome configuration

```yaml
esphome-radio-bridge:
  driver: tubeszb_mgm210_poe_2023
  mode:
    name: EFR32 Operating Mode
  uart_mode_select:
    name: EFR32 UART Test Mode
  uart_mode_status:
    name: EFR32 UART Mode Change Status
  flow_control_wiring_verified: false  # keep false until tested electrically
```

`flow_control_wiring_verified: false` permits two-wire operation and rejects
four-wire changes. After verifying the physical routes, set it to `true`,
compile, and OTA-update the ESP32 once. This is an explicit operator opt-in,
not an indication that the component can independently detect wiring.

| Select option | ESP32 UART1 RTS output | ESP32 UART1 CTS input | Intended experiment |
| --- | --- | --- | --- |
| `2-wire (TX/RX)` | Disabled | Disabled | Known-good baseline |
| `4-wire (profile RTS/CTS)` | GPIO2 | GPIO13 | Silk-screen names refer to ESP32 |
| `4-wire (swapped RTS/CTS)` | GPIO13 | GPIO2 | Silk-screen names refer to EFR32 |

The options test *ESP32 assignments*, not EFR32 firmware variants. Neither
option automatically re-flashes the radio. A four-wire firmware image should
only be applied after its flow-control requirements and actual pin routing
have been verified.

## Reversible hardware test procedure

1. Back up the Zigbee network and record current EFR32 version, UART mode,
   radio identification and ZHA status. Keep the working two-wire `.gbl` image
   accessible for recovery.
2. Physically verify the RTS/CTS traces before enabling the four-wire opt-in.
   Inspect any strap/pull resistor on GPIO2. If unresolved, **stop the spike**.
3. Deploy the spike-enabled ESP32 firmware while the EFR32 still runs known-good
   two-wire NCP firmware; verify `2-wire (TX/RX)` and ZHA operation.
4. Disconnect/stop ZHA so TCP port 6638 has no client. Disarm firmware flashing
   and allow any in-progress identification to complete.
5. Flash the confirmed compatible four-wire EFR32 firmware using the existing
   button/verified manifest. **Do not alter the bootloader UART mode.**
6. With ZHA disconnected, select the electrically verified four-wire mapping.
   The bridge applies pin routing, resets the radio, and probes it before
   reopening the TCP listener. Check the `EFR32 Operating Mode` sensor and
   EFR32 identification. Only then start ZHA and validate traffic/reconnects.
7. To revert, stop ZHA, return the selector to two-wire, and if the NCP image
   requires hardware flow control, flash the saved two-wire EFR32 firmware
   through the two-wire Gecko bootloader. Restart ZHA after the probe settles.

**Important recovery boundaries:** An ESP32 reboot resets the *selector* to
its board-profile default (two-wire), but it **does not** revert EFR32 firmware.
If the EFR32 is running four-wire-only NCP firmware after an ESP32 reboot,
choose the verified four-wire mapping again from Home Assistant, or flash the
known-good two-wire radio image. Do not assume a reboot is sufficient to
restore Zigbee connectivity.

Changing the UART select entity while a TCP client is connected, maintenance
is armed, a probe is running, or firmware flashing is active is rejected. The
`uart_mode_status` entity explains rejection. For an ESP32 restart, the default
profile is deliberately non-persistent to keep an operator-accessible baseline.

## Acceptance criteria

- Two-wire normal mode remains unchanged and restores after ESP32 restart.
- Both four-wire assignments can be selected *only after explicit wiring opt-in*.
- Selecting a mode with a connected ZHA client or active flash is rejected.
- Mode switching updates the select state and existing `mode` diagnostic.
- The Gecko bootloader and firmware flashing remain two-wire for all options.
- Switching back to two-wire releases RTS/CTS GPIO routes, and radio probe/
  TCP listener recovery works without rebooting the ESP32.
- A full ESPHome 2026.10.2 Docker compile and physical hardware trial must pass
  before merging. Host tests do not validate board-level electrical routing.

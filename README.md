# Desk Clock

Analog desk clock firmware for the Open Nextion ONX2424G013 knob, for use with Home Assistant.

- `desk-clock.yaml`: my clock (Wi-Fi and API key from secrets.yaml).
- `desk-clock-share.yaml`: firmware to give away. No Wi-Fi or keys built in; the clock shows a
  QR code for its "Desk Clock Setup" hotspot, then "Add me in Home Assistant", and Home Assistant
  creates and installs the API key itself when it's added. `release/` holds factory images for
  https://web.esphome.io.
- `clock_device.yaml`: hardware, entities and setup-screen logic shared by both.
- `sim.yaml`: SDL preview (Up/Down = dial, Space = press, R = rotate, S = second hand, W = setup screens).
- `clock_face.h`: the clock face. `setup_screen.h`: first-run screens. `clock_common.yaml`: fonts and settings.
- Build: `~/Work/sat1-beam4-firmware/.venv/bin/esphome compile desk-clock.yaml`
- Flash: `... esphome upload desk-clock.yaml --device <ip>`

## Using the knob in automations

The dial is relative: it says which way it turned, never a position, so it always
works from the controlled entity's current state.

For brightness, volume and the like, use the batched Home Assistant event
`esphome.desk_clock_dial`. Its `steps` is the dial movement in the last 200 ms,
2 per click (`dial_step` in desk-clock.yaml; positive = clockwise), so a fast spin makes a few big changes instead of
queueing dozens of light commands. This needs "Allow the device to perform Home
Assistant actions" ticked in the device's ESPHome options.

`event.desk_clock_dial` also fires once per detent (`clockwise` / `anticlockwise`),
which is handy in the automation editor for one-step things like next/previous track.

```yaml
automation:
  - alias: Desk clock dial dims the living room
    mode: queued
    triggers:
      - trigger: event
        event_type: esphome.desk_clock_dial
    actions:
      - action: light.turn_on
        target: { entity_id: light.living_room_lights }
        data:
          brightness_step_pct: "{{ (trigger.event.data.steps | int) * 4 }}"

  - alias: Desk clock button toggles the living room
    triggers:
      - trigger: state
        entity_id: event.desk_clock_button
        not_from: unavailable
    conditions:
      - "{{ trigger.to_state.attributes.event_type == 'single_press' }}"
    actions:
      - action: light.toggle
        target: { entity_id: light.living_room_lights }
```

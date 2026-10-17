# Desk Clock

Analog desk clock firmware for the Open Nextion ONX2424G013 knob, for use with Home Assistant.

- `desk-clock.yaml`: device firmware. `sim.yaml`: SDL preview (Up/Down = dial, Space = press, R = rotate, S = second hand).
- `clock_face.h`: the clock face (LVGL draw calls). `clock_common.yaml`: shared fonts and settings.
- Build: `~/Work/sat1-beam4-firmware/.venv/bin/esphome compile desk-clock.yaml`
- Flash: `... esphome upload desk-clock.yaml --device <ip>`

## Using the knob in automations

The dial sends one `event.desk_clock_dial` event per detent; use `mode: queued` so fast turns aren't dropped.

```yaml
automation:
  - alias: Desk clock dial sets the desk lamp
    mode: queued
    triggers:
      - trigger: state
        entity_id: event.desk_clock_dial
        not_from: unavailable
    actions:
      - action: light.turn_on
        target: { entity_id: light.desk_lamp }
        data:
          brightness_step_pct: "{{ 5 if trigger.to_state.attributes.event_type == 'clockwise' else -5 }}"

  - alias: Desk clock button toggles the desk lamp
    triggers:
      - trigger: state
        entity_id: event.desk_clock_button
        not_from: unavailable
    conditions:
      - "{{ trigger.to_state.attributes.event_type == 'single_press' }}"
    actions:
      - action: light.toggle
        target: { entity_id: light.desk_lamp }
```

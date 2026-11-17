# Home Assistant protection flags

The firmware automatically discovers **Overcurrent** and **Overvoltage** binary
sensors under the existing TechPulse Energy Monitor device.

Every meter sample (two seconds), it publishes JSON to
`esp32/<lowercase MAC without colons>/protection`:

```json
{"overcurrent":"ON","overvoltage":"OFF"}
```

- Overcurrent is ON when calibrated current exceeds 1 A.
- Overvoltage is ON when calibrated voltage exceeds 235 V.
- Each flag clears on the next valid reading at or below its limit. These are
  current conditions, not a history of relay trips. The relay still requires a
  new ON command after a trip.
- Invalid readings or missing calibration send `None` for the affected flag,
  which Home Assistant displays as unknown. Missing updates expire after ten
  seconds; device availability follows the existing MQTT availability topic.
- Discovery configuration is retained; flag updates are not retained.

After uploading the firmware, the sensors appear through MQTT discovery. They
can be used in dashboards and as ON-state triggers for notification automations.

Reference: [Home Assistant MQTT binary sensor](https://www.home-assistant.io/integrations/binary_sensor.mqtt/).

# PT1000 Temperature Module (ComWinTop 8PT1000)

8-channel PT100/PT1000 temperature acquisition module with RS485 Modbus RTU interface.

## Specifications

| Parameter | Value |
|-----------|-------|
| Manufacturer | ComWinTop |
| Part Number | 8PT1000 |
| Channels | 8 |
| Sensor Type | PT100 or PT1000 (2/3 wire) |
| Measuring Range | -180°C to +650°C |
| Resolution | 0.1°C |
| Accuracy | 0.25°C |
| Output | RS485 (Modbus RTU) |
| Power Supply | DC 8~30V |
| Power Consumption | 9mA@30V, 12mA@24V, 23mA@12V, 33mA@8V |
| Working Environment | -30°C to +55°C, 0-95% RH |
| Mounting | 35mm DIN rail |
| Dimensions | 88 x 72 x 59 mm |

## Default Communication Settings

| Parameter | Default Value |
|-----------|---------------|
| Slave Address | 1 |
| Baud Rate | 9600 |
| Parity | None |
| Data Bits | 8 |
| Stop Bits | 1 |

## Terminal Connections

| Terminal | Description |
|----------|-------------|
| +V | Power + (8-30V DC) |
| GND | Power - |
| RTDx+ | PT100/PT1000 + (channel x) |
| RTDx- | PT100/PT1000 - (channel x) |
| GND | PT100/PT1000 GND (for 3-wire) |
| A (D+) | RS485 + |
| B (D-) | RS485 - |

**Note**: For 2-wire PT1000, short RTD(n)- to GND.

## Modbus Register Map

### Parameter Registers (Function 03H read, 06H write)

| Address | Byte | Meaning | Description | Property |
|---------|------|---------|-------------|----------|
| 0x10 | LO | Communication | BIT<7:5>: reserved<br>BIT<4:3>: Parity (00=none, 01=even, 10=odd)<br>BIT<2:0>: Baud (000=9600, 001=1200, 010=2400, 011=4800, 100=9600, 101=14400, 110=19200) | RW |
| 0x10 | HI | Address | Slave address 1-250 (default: 01) | RW |

**Note**: Parameters can only be written within 255 seconds after power-up.

### Data Registers (Function 03H read)

| Register | Description | Data Type | Unit |
|----------|-------------|-----------|------|
| 0 | Channel 1 Temperature | Signed Int16 | 0.1°C |
| 1 | Channel 2 Temperature | Signed Int16 | 0.1°C |
| 2 | Channel 3 Temperature | Signed Int16 | 0.1°C |
| 3 | Channel 4 Temperature | Signed Int16 | 0.1°C |
| 4 | Channel 5 Temperature | Signed Int16 | 0.1°C |
| 5 | Channel 6 Temperature | Signed Int16 | 0.1°C |
| 6 | Channel 7 Temperature | Signed Int16 | 0.1°C |
| 7 | Channel 8 Temperature | Signed Int16 | 0.1°C |

### Example: Read Channel 1

Request (address 1, read 1 register from 0):
```
01 03 00 00 00 01 84 0A
```

Response (temperature = 25.0°C = 250 = 0x00FA):
```
01 03 02 00 FA B8 44
```

## Sensor Detection (Autodetect)

### Raw Value Ranges

| Condition | Raw Value | Temperature | Meaning |
|-----------|-----------|-------------|---------|
| Short circuit | < -1385 | < -138.5°C | Sensor shorted or fault |
| Valid sensor | -1385 to +6085 | -138.5°C to +608.5°C | Normal operation |
| Open circuit | > +6085 | > +608.5°C | No sensor connected |

### Detection Logic

```cpp
bool is_valid_pt1000_raw(int16_t raw_value) {
    // 5% to 95% of range (-1800 to +6500)
    const int16_t LOW_THRESHOLD = -1385;   // -138.5°C
    const int16_t HIGH_THRESHOLD = 6085;   // +608.5°C
    return (raw_value >= LOW_THRESHOLD && raw_value <= HIGH_THRESHOLD);
}
```

Typical fault values:
- **Open circuit**: ~6500 (650°C) or 0x7FFF
- **Short circuit**: ~-1800 (-180°C) or 0x8000

## Configuration Example

```json
{
  "modbus": {
    "devices": [
      {
        "address": 1,
        "type": "pt1000_8ch",
        "name": "PT1000 Module",
        "registers": [
          {"name": "fermenter_1_temp", "reg": 0, "scale": 0.1, "unit": "°C"},
          {"name": "fermenter_2_temp", "reg": 1, "scale": 0.1, "unit": "°C"},
          {"name": "glycol_supply", "reg": 2, "scale": 0.1, "unit": "°C"},
          {"name": "glycol_return", "reg": 3, "scale": 0.1, "unit": "°C"}
        ]
      }
    ]
  }
}
```

## Troubleshooting

### No Response
- Check RS485 wiring (A/B polarity)
- Verify slave address and baud rate
- Check power supply (8-30V DC)
- Enable 120Ω termination resistor if needed

### Reading Shows Max/Min Value
- **+650°C / 6500**: Open circuit - check sensor wiring
- **-180°C / -1800**: Short circuit - check for shorts to GND

### Inaccurate Readings
- Verify PT100 vs PT1000 sensor type matches module setting
- Check 2-wire vs 3-wire configuration
- Ensure proper grounding

## Resources

- [Datasheet (PDF)](PT1000_Datasheet.pdf)
- Manufacturer: ComWinTop
- ASIN: B0BX5LCKZ9

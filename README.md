# LoRaWAN Livestock Health and Location Monitoring System

An end-to-end LoRaWAN system for monitoring cattle body temperature and location, covering the ear-tag sensor node, the radio link, the gateway, and a real-time web dashboard.

The system is designed for extensive grazing conditions, where animals are dispersed over large areas with no cellular coverage and no mains power, and where a herder cannot practically inspect every animal every day. Early detection of a temperature anomaly and knowledge of an animal's position are the two pieces of information that matter most in that setting.

**Status:** functional prototype. All four subsystems are implemented and working end to end. See Limitations for what is not yet in place.

---

## System architecture

```
  Ear tag node                    Gateway                    Server
  ------------                    -------                    ------
  ESP32-C6                                                   ChirpStack
  DS18B20  (temperature)   -->    RAK5146 PiHAT       -->    (self-hosted)
  NEO-6M   (GNSS)                 Raspberry Pi                    |
  RFM95W   (LoRa)                                             MQTT
  12,000 mAh Li-ion                                               |
                                                                  v
         LoRaWAN uplink, 4x per day                        Flask dashboard
                                                            + CSV logging
```

---

## Ear tag node

Battery-powered sensor node worn as an ear tag.

| Function | Part |
|---|---|
| MCU | ESP32-C6 |
| Body temperature | DS18B20 (1-Wire) |
| Localisation | u-blox NEO-6M GNSS |
| Radio | RFM95W (LoRa, 868 MHz, SF7) |
| Power | Single 12,000 mAh 3.7 V Li-ion cell |

Firmware is written in C++ using the Arduino framework.

The LoRaWAN ABP uplink path is implemented directly in the firmware, including AES-128 payload encryption and the CMAC-based message integrity check, rather than through a LoRaWAN stack library. This was done to work through the MAC layer explicitly. A production build would use an audited implementation.

### Operating cycle

The node spends almost all of its life in deep sleep. Four times per day it wakes, reads body temperature, acquires a GNSS fix, transmits a single LoRaWAN uplink, and returns to sleep.

Deep sleep on the ESP32-C6 clears RAM and restarts execution from the top, so the entire measurement cycle runs in `setup()` and `loop()` is unused. The LoRaWAN frame counter is held in RTC memory so that it survives sleep; without this the network server would reject every uplink as a replay.

The GNSS receiver is placed in UBX backup mode before sleeping rather than left running. Fix acquisition is bounded by a timeout, and the payload carries a validity flag so the decoder can distinguish a real position from a cycle that ended without a fix. A node under heavy tree cover therefore still reports its temperature instead of draining the cell waiting for satellites.

### Temperature interpretation

The dashboard currently classifies each reading against fixed thresholds: below 35 °C, normal to 38.5 °C, warning to 39.5 °C, and fever above that.

These thresholds are a placeholder, and the limits of the approach are worth stating. The DS18B20 is specified at ±0.5 °C, and an ear tag measures at a site exposed to solar radiation and wind. A fixed cut-off will therefore raise false fever alerts on a hot afternoon and miss real ones at night.

The intended replacement is alerting on sustained deviation from each animal's own baseline, which absorbs both the sensor offset and the individual animal. That logic is not yet implemented.

### Power budget

Sizing assumptions for the 12,000 mAh cell:

| Item | Estimate |
|---|---|
| GNSS fix | ~30 mA for 30 s per cycle |
| LoRa transmit (SF7, 24-byte frame) | ~100 mA for ~60 ms per cycle |
| Sensing and processing | ~40 mA for a few seconds per cycle |
| Deep sleep | board-level quiescent current |
| Cycles per day | 4 |

Transmission is negligible in this budget; the GNSS fix dominates the active cycle. Four cycles per day therefore consume only a few mAh, and on these assumptions the node supports well over two years of operation.

The dominant unknown is board-level quiescent current, not the active cycle. Measuring true sleep current on the assembled board is the single most useful validation step for this figure.

---

## Custom PCB

KiCad design files for the node, including the RFM95W transceiver front-end.

RF layout was the main constraint: ground plane continuity under the transceiver, trace impedance on the antenna feed, antenna placement relative to the enclosure and to the animal's body, and supply decoupling close to the module. The antenna is a soldered helical spring rather than a connectorised u.FL or SMA part, so its position is fixed at assembly and had to be planned around the rest of the layout.

A separate RFM95W breakout board was also designed and fabricated to adapt the module's 2 mm pin pitch to a standard 2.54 mm grid, for bench prototyping and node integration. It carries the module and its pin headers only, and is supplied with regulated 3.3 V from the host board.

Files are in `hardware/`.

---

## Gateway

- **RAK5146 PiHAT Kit** on a Raspberry Pi
- Forwards LoRaWAN uplinks to the network server

Configuration notes are in `gateway/`.

---

## Network server

**ChirpStack**, self-hosted. Handles device provisioning, session keys, deduplication of uplinks received by multiple gateways, and payload decoding.

The device payload decoder is in `chirpstack/`. The uplink carries body temperature, latitude, longitude, and a GNSS validity flag.

---

## Web application

A **Python / Flask** application subscribes to the ChirpStack MQTT broker on the uplink topic for the application, decodes each event, and serves a dashboard showing every device with its latest temperature, its last known position as a Google Maps link, and the time it was last heard from. The page refreshes on a timer, and temperature values are colour-coded by band.

Every uplink is appended to a CSV file, which is the historical record and the export path for offline analysis.

Source is in `webapp/`.

---

## Repository layout

```
firmware/
  Cow_monitoring.ino  ESP32-C6 node firmware (Arduino)
  secrets.h.example   template for LoRaWAN session keys
hardware/
  node/               KiCad schematic and PCB for the ear tag node
  rfm95-breakout/     KiCad files for the RFM95W pitch-adapter board
gateway/              Raspberry Pi + RAK5146 configuration
chirpstack/           payload decoder and device profile
webapp/               Flask dashboard and MQTT subscriber
```

Real LoRaWAN session keys live in `firmware/secrets.h`, which is gitignored. Copy `secrets.h.example` and fill in your own device keys from ChirpStack before flashing.

---

## Limitations

Stated plainly, because they matter to anyone considering this work.

- **Fixed temperature thresholds.** Alerting uses absolute cut-offs rather than a per-animal baseline, which is not adequate for an ear-tag measurement site. See Temperature interpretation above.
- **No database.** History lives in a CSV file. The dashboard itself holds only the latest reading per device in memory, so it is blank after a restart until the next uplink arrives, which may be six hours away.
- **No field validation against a reference.** Temperature readings have not been validated against a rectal or ruminal reference measurement on live animals, which is the standard against which any livestock temperature system must eventually be judged.
- **Battery life is calculated, not measured.** The two-year figure follows from the power budget above. It has not been confirmed by long-duration testing, and board-level sleep current has not yet been measured directly.
- **Hand-rolled cryptography.** The AES-128 and CMAC implementations are the author's own and have not been independently audited. They are appropriate for a prototype, not for a deployed product.
- **NEO-6M cold start.** The module is an older generation with slow cold-start acquisition. Retaining almanac data across wake cycles would reduce time to fix and further improve the energy margin.
- **Single-tag testing.** The system has not been exercised with a herd-scale number of nodes, so airtime contention and duty-cycle behaviour at scale remain untested.

---

## Next steps

- Measure board-level quiescent current and confirm the energy budget empirically
- Replace fixed thresholds with per-animal baseline tracking and deviation-based alerting
- Move from CSV to a time-series database, and reload the dashboard state on restart
- Validate temperature against a reference measurement on live animals
- Test with multiple nodes to characterise airtime and collision behaviour

---

## Author

Wendyam Clovis Dubois Zongo
MSc Electrical Engineering (Computer Engineering), PAUSTI / JKUAT, Nairobi
Embedded systems and FPGA engineer

# PPS Connection

## Purpose

Use the ZED-F9T's precision time-pulse output to control the TeensyTimeServer clock. With the SparkFun MicroMod Main Board - Single v2.1 (DEV-20748), the pulse can reach the Teensy MicroMod without replacing the carrier or soldering directly to the processor module.

## Recommended Wiring

Power down the assembly before making these connections.

```text
ZED-F9T TP1 PTH  -------- DEV-20748 RXI PTH
DEV-20748 SEL PTH ------- DEV-20748 GND PTH
ZED-F9T GND ------------- DEV-20748 GND (recommended)
```

The existing Qwiic connection already provides a common ground. A short ground wire routed beside the TP1 signal wire is still recommended for better signal integrity.

## Why This Connection Works

- Pulling the Main Board's `SEL` signal LOW routes the `RXI` plated-through hole through the carrier's UART multiplexer to the processor's primary UART receive signal.
- On the Teensy MicroMod, the primary UART receive signal maps to Arduino digital pin `0`.
- Although this signal is labeled as UART receive, Teensy pin `0` can be configured as an ordinary interrupt-capable GPIO input.
- The current TeensyTimeServer firmware does not use `Serial1`, leaving pin `0` available.
- The W5500 Ethernet Function Board uses SPI plus interrupt, reset, and chip-select signals. It does not use the primary UART, so selecting the external `RXI` route should not interfere with Ethernet.

## ZED-F9T Requirements

- Use the labeled `TP1` plated-through hole on the ZED-F9T breakout.
- Leave the ZED-F9T's `TP1` solder jumper **closed**. Opening it disconnects the TP1 PTH from the time-pulse signal.
- The `TP1_LED` jumper may optionally be opened to remove the status LED load. Do not confuse it with the `TP1` signal-routing jumper.
- TP1 is an actively driven 3.3 V output. No level shifter or pull-up resistor is required for the Teensy input.
- Do not use a 50-ohm termination; the Teensy input must remain high impedance.
- Keep the TP1-to-RXI wire short. A short signal-and-ground twisted pair is preferable.

Configure TP1 explicitly for:

- Enabled output
- 1 Hz frequency
- Rising-edge polarity
- Alignment to the top of each second
- GNSS synchronization
- UTC time grid

The ZED-F9T factory TP1 configuration is nominally 1 Hz but uses the GPS time grid, so the UTC time grid must be selected explicitly for the NTP application.

## Firmware Entry Point

The eventual GPIO configuration would be similar to:

```cpp
constexpr uint8_t PPS_PIN = 0;

pinMode(PPS_PIN, INPUT);
attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsInterrupt, RISING);
```

Use `INPUT`, not `INPUT_PULLUP`, because TP1 is actively driven. The interrupt handler should capture a hardware counter or timestamp and return immediately. It should not perform I2C transactions, logging, or NTP packet construction.

The carrier's UART multiplexer and wiring add a small, mostly fixed propagation delay. This is far smaller and more repeatable than the timing uncertainty from polling the GNSS over I2C. It can be measured and compensated later if absolute sub-microsecond calibration is required.

## References

- [SparkFun MicroMod Main Board Hookup Guide V2](https://learn.sparkfun.com/tutorials/micromod-main-board-hookup-guide-v2/hardware-overview)
- [SparkFun MicroMod Teensy Processor Hookup Guide](https://learn.sparkfun.com/tutorials/micromod-teensy-processor-hookup-guide/all?print=1)
- [SparkFun MicroMod Ethernet Function Board - W5500 Hookup Guide](https://learn.sparkfun.com/tutorials/micromod-ethernet-function-board---w5500-hookup-guide/all)
- [SparkFun GNSS Timing Breakout - ZED-F9T Hookup Guide](https://learn.sparkfun.com/tutorials/gnss-timing-breakout---zed-f9t-qwiic-hookup-guide/all)
- [u-blox ZED-F9T-00B Data Sheet](https://content.u-blox.com/sites/default/files/ZED-F9T-00B_DataSheet_UBX-18053713.pdf)
- [u-blox ZED-F9T Interface Description](https://content.u-blox.com/sites/default/files/ZED-F9T_InterfaceDescription_%28UBX-18053584%29.pdf)

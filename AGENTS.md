# Repository Guidelines

## Project Goal

Build a self-contained NTP time-server appliance that delivers extremely accurate GPS-derived time. Favor designs that minimize latency from the GPS receiver, through timestamp processing and the Ethernet stack, to the NTP client without sacrificing correctness, reliability, or standards compliance.

## Project Structure & Module Organization

`TeensyTimeServer.ino` is the firmware entry point and initializes Ethernet, GNSS, RTC, OLED, HTTP, and NTP services. Root-level module pairs keep responsibilities focused: `Properties.*` manages EEPROM-backed settings, `TimeData.*` handles time state, and `TimeHttp.*` implements the web interface. `docs/` contains documentation. `TimeHttp.cpp.org` is an uncompiled legacy backup; `src/` is reserved for future reorganization. Visual Micro generates `__vm/` and much of the Visual Studio project metadata.

## Build, Test, and Development Commands

- In Visual Studio 2026, use **Extensions > vMicro > Open Arduino Project** and choose `TeensyTimeServer.ino`. Do not open the generated `.vcxproj` directly; that bypasses Arduino/Visual Micro setup and can break header discovery.
- Select **Arduino 2**, **Teensy MicroMod (`teensyMM`)**, and the correct port. Use Visual Micro **Build** to verify and **Build & Upload** to flash. Do not use Visual Studio's native **Build Solution**, which treats this as a remote Linux C++ project.
- Run `arduino-cli compile --fqbn teensy:avr:teensyMM --libraries C:\Development\Arduino\libraries .` for an independent clean build.

Libraries come from the Teensy package under `%LOCALAPPDATA%\Arduino15\packages\teensy\` and the sketchbook at `C:\Development\Arduino\libraries`. Keep required Arduino library includes visible in the main sketch so Visual Micro can generate precise IntelliSense paths. Do not add broad Arduino package roots; similarly named libraries for other boards may be selected.

## Coding Style & Naming Conventions

Use two-space indentation, same-line opening braces, `camelCase` functions and variables, `PascalCase` classes, and `UPPER_SNAKE_CASE` constants. Pair headers with implementations, use `#pragma once`, preserve MIT headers, and prefer fixed-width integers for packet and EEPROM layouts.

## Testing Guidelines

Every change must compile cleanly. For hardware changes, verify boot, DHCP/static networking, GNSS lock, RTC synchronization, OLED output, HTTP configuration, and an NTP query. Record the board, library versions, and results in the pull request. Put future host tests in `tests/`, named like `TimeDataTests.cpp`.

## Commit & Pull Request Guidelines

Use short imperative subjects, such as `Fix NTP fraction conversion`, and separate unrelated changes. Pull requests should explain behavior and risk, link issues, list build/hardware validation, and include screenshots for HTTP or OLED changes. Never commit credentials, generated binaries, caches, or local IDE paths.

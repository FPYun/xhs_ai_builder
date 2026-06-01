# App Companion UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve the firmware-hosted `/app` mobile page using established mobile companion and device-control UI patterns.

**Architecture:** Keep the firmware HTTP API local and lightweight. Rework only the embedded HTML/CSS/JS in `04_camera_pet_battle.ino`, then document the changed navigation and event behavior in `docs/app-http-api.md`.

**Tech Stack:** ESP32 Arduino `WebServer`, embedded vanilla HTML/CSS/JS, existing PowerShell compile/upload scripts.

---

### Task 1: Navigation and Page Structure

**Files:**
- Modify: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`

- [x] Keep friends as a first-level bottom tab because social interaction is a core project pillar.
- [x] Use six top-level destinations: home, capture, bag, battle, friends, more.
- [x] Make the bottom tab bar resilient on phone browsers with delegated `pointerup` and `click` handling.
- [ ] Keep all actions routed through existing `/api/v1/action` endpoints.

### Task 2: More Page and Event Filtering

**Files:**
- Modify: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`

- [ ] Add more-page sections for friends, events, and settings.
- [ ] Add client-side event filters: all, capture, battle, friend, setting, system.
- [ ] Keep the last-20-event detail view.

### Task 3: Home and Backpack Refinement

**Files:**
- Modify: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`

- [ ] Add a compact next-action area to home.
- [ ] Keep backpack as one-pet swipe cards.
- [ ] Add clearer XP/win-rate progress presentation without changing storage.

### Task 4: Documentation and Verification

**Files:**
- Modify: `docs/app-http-api.md`

- [ ] Update `/app` navigation documentation.
- [ ] Run embedded JavaScript syntax check.
- [ ] Run `.\scripts\check-module3-ui.ps1`.
- [ ] Compile with `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_app_companion_ui`.
- [ ] Upload to COM7 with the same build root when compile succeeds.

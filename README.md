# Cyber-Grid ⚡ v8.0

**Enterprise-Grade Dual-Node Smart Grid Controller with Hybrid Networking & Edge-AI Voice Integration.**

Cyber-Grid is an advanced, resilient smart power management system built on the Arduino UNO R4 WiFi. It features live high-voltage telemetry, dual-redundant voice control (Cloud + Edge AI), a dynamic web-based enterprise UI, and an automatic offline network failsafe, making it completely immune to internet outages.

---

## 🌟 Key Features

* 📊 **High-Voltage Telemetry:** Real-time monitoring of Voltage (V), Current (A), Power (W), Energy (kWh), Frequency, and Power Factor via a PZEM-004T sensor.
* 🌐 **Hybrid Networking:** Connects to local Wi-Fi normally, but automatically broadcasts its own Offline Access Point (`Cyber-Grid-Offline`) if the router fails, ensuring zero downtime.
* ☁️ **Cloud Smart-Home Integration:** Fully integrated with **Sinric Pro** for seamless Amazon Alexa and Google Assistant control from anywhere in the world.
* 🎙️ **Offline Edge-AI Voice Control:** Zero-latency, completely offline voice triggers using a custom-flashed **Ai-Thinker VC-02** module. Features Edge-Detection logic and internal pull-up resistors for total immunity to electrical noise and "ghost triggers."
* 📱 **Enterprise Web Dashboard:** A responsive, mobile-first HTML/JS dashboard hosted directly on the Arduino. Includes live `Chart.js` power graphing, dynamic SVG timer rings, cost projections, and schedule configurations.
* 🛡️ **Auto-Limit Safety:** Independent safety limits for each node. If power draw exceeds the set wattage, the grid automatically terminates the link and sounds the alarm.
* 💾 **Persistent Memory:** EEPROM integration saves financial tariffs, safety limits, and automation states across reboots and power losses.

---

## 🛠️ Hardware Architecture

### Components Required
* **Microcontroller:** Arduino UNO R4 WiFi
* **Power Sensor:** PZEM-004T (v3.0 / v4.0)
* **Relays:** 2-Channel 5V Relay Module (Opto-isolated)
* **Display:** 0.96" I2C OLED Display (SSD1306)
* **Voice AI:** Ai-Thinker VC-02 Offline Voice Module
* **Audio:** Active Buzzer (5V)

### 🔌 Master Pin Connection Guide

| Component | Arduino UNO R4 Pin | Notes |
| :--- | :--- | :--- |
| **Relay 1** (Node 1) | **D2** | Controls your first outlet/appliance (Active LOW). |
| **Relay 2** (Node 2) | **D7** | Controls your second outlet/appliance (Active LOW). |
| **Buzzer** | **D4** | For system alerts and voice confirmations. |
| **PZEM-004T (RX)** | **D0** (TX) | *Crucial:* Sensor RX connects to Arduino TX. |
| **PZEM-004T (TX)** | **D1** (RX) | *Crucial:* Sensor TX connects to Arduino RX. |
| **OLED Display (SDA)** | **A4** | The dedicated I2C data highway. |
| **OLED Display (SCL)** | **A5** | The dedicated I2C clock highway. |
| **VC-02 (Pin B2)** | **D9** | Triggers Node 1 (Input Pullup, Edge Detection). |
| **VC-02 (Pin A27)**| **D11** | Triggers Node 2 (Input Pullup, Edge Detection). |

> **⚠️ Critical Power Routing Note:** The VC-02 Voice module is powered directly from the wall using its own USB-C cable to prevent current starvation (brownouts) when the 2W speaker spikes. It **must** share a common `GND` wire with the Arduino for the trigger pins to work.

---

## 🧠 Flashing Custom Edge-AI Firmware (VC-02)

Out of the box, the Ai-Thinker VC-02 comes with generic factory firmware. To make it communicate properly with the Cyber-Grid's edge-detection logic and internal pull-up resistors, you must flash it with custom firmware.

### Step 1: Build the Firmware on the Portal
1. Navigate to the [Ai-Thinker Smart Voice Platform](https://smartvoice.ai-thinker.com/) and create a new project for the **VC-02** chip.
2. **Define the Wake Word:** Set your custom activation phrase under Vocabulary (e.g., *"Hello Cyber Grid"*).
3. **Map the Hardware Pins (Critical):**
   * Go to the **Control Details** tab.
   * Command 1 (*"Turn on Grid One"*): Set action to control **`GPIO_B2`** -> Parameter: **`low level trigger`** (Pulse, ~500ms).
   * Command 2 (*"Turn on Grid Two"*): Set action to control **`GPIO_A27`** -> Parameter: **`low level trigger`** (Pulse, ~500ms).
   * *Note: Using a "low level" trigger is absolutely mandatory to trigger the Arduino's `INPUT_PULLUP` safely without getting the relays stuck in a permanent loop.*
4. Click **Build** at the bottom of the page and download the generated `.bin` firmware file.

### Step 2: Burner Tool Usage

*(Note: Ensure the VC-02 module is completely disconnected from the Arduino before flashing.)*

[ **TODO:** Insert screenshots and specific step-by-step instructions for using the Ai-Thinker Serial Burning Tool here... ]

---

## 🚀 Installation & Setup

1. **Arduino Libraries:** Ensure `PZEM004Tv40`, `U8g2`, `NTPClient`, and `SinricPro` are installed via the Arduino Library Manager.
2. **Cloud Configuration:** Create a free account at [Sinric.pro](https://sinric.pro/), create two "Smart Switch" devices, and update the `APP_KEY`, `APP_SECRET`, `NODE1_ID`, and `NODE2_ID` variables in the code.
3. **Network Configuration:** Update the `ssid` and `pass` variables with your local Wi-Fi credentials.
4. **Flash the Board:** Upload the master build to your Arduino UNO R4 WiFi.

## 🌐 Usage & Interface
Once booted, the OLED screen will display the assigned IP address. Enter this IP into any web browser on the same network to access the Enterprise Dashboard. 
* **Offline Failsafe:** If your router fails, connect your device to the `Cyber-Grid-Offline` Wi-Fi network (Password: `12345678`) and navigate to `192.168.4.1`.

---
*Built for Hackathons. Built for Resilience.* ⚡

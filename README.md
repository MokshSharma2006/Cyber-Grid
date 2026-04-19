# Cyber-Grid ⚡ 

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

## 💾 EEPROM Persistent Memory Protection

Cyber-Grid utilizes the Arduino UNO R4's internal EEPROM (Electrically Erasable Programmable Read-Only Memory) to provide non-volatile storage for critical system parameters. This ensures that the grid autonomously recovers its exact configuration after a power outage or hard reboot without requiring user intervention.

**Persistently Stored Variables:**
* **Financial Tariff Rate (`ADDR_TARIFF`):** Saves the custom ₹/kWh rate set via the dashboard for persistent session cost calculations.
* **Node Safety Limits (`ADDR_LIMIT1`, `ADDR_LIMIT2`):** Retains the precise maximum wattage overload thresholds defined by the user for Node 1 and Node 2.
* **Auto-Limit State Toggles (`ADDR_AUTO1`, `ADDR_AUTO2`):** Remembers whether the automatic safety trip mechanism was armed or disarmed for each independent node.

**Operational Flow:**
Whenever a configuration change is made via the Web UI, the new value is instantly committed to the EEPROM. During the `setup()` boot sequence, Cyber-Grid reads these memory addresses *before* initializing the high-voltage relays. This guarantees the system never powers on in an unprotected or incorrect state. A "WIPE SYSTEM" factory reset button is available in the dashboard settings to format these memory blocks and restore default values.

---

## 🛠️ Hardware Architecture

### Components Required
* **Microcontroller:** Arduino UNO R4 WiFi
* **Power Sensor:** PZEM-004T (v3.0 / v4.0)
* **220V Socket:** 2-3pin Sockets
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

## ⚠️ Mains Connection

### ⚡High-Voltage AC Connection 


                 AC MAINS
              ┌─────────────┐
              │             │
         LIVE (L)       NEUTRAL (N)
            │                │
            │                ├───────────────┬───────────────┐
            │                │               │               │
            │           ┌────▼────┐     ┌────▼────┐     ┌────▼────┐
            │           │  PZEM   │     │ DEVICE 1│     │ DEVICE 2│
            │           │   N     │     │ NEUTRAL │     │ NEUTRAL │
            │           └─────────┘     └─────────┘     └─────────┘
            │
            ▼
      ┌────────────┐
      │   RELAY 1  │
      │  COM   NO  │
      └──┬─────┬───┘
         │     │
         │     ▼
         │   ( CT CLAMP )
         │       │
         │       ▼
         │   DEVICE 1 LIVE
         │
         │
         ▼
      ┌────────────┐
      │   RELAY 2  │
      │  COM   NO  │
      └──┬─────┬───┘
         │     │
         │     ▼
         │   ( CT CLAMP )
         │       │
         │       ▼
         │   DEVICE 2 LIVE
         │
         └──────────────→ PZEM V (Voltage input)


---

## ☁️ Sinric Pro Cloud Configuration (Alexa/Google Integration)

To enable cloud voice controls, you must link the Arduino to the Sinric Pro platform. 

### Step 1: Create Your Devices
1. Go to [Sinric.pro](https://sinric.pro/) and create a free account.
2. Navigate to **Devices** -> **Add Device**.
3. Create your first device:
   * **Name:** `Grid Node One` (This is the name Alexa will use).
   * **Device Type:** `Smart Switch`.
   * Click **Save**.
4. Repeat the process to create a second device named `Grid Node Two`.

### Step 2: Get Your Device IDs
1. On your Sinric Pro Devices dashboard, you will see your two new switches.
2. Copy the **Device ID** for `Grid Node One` and paste it into the `NODE1_ID` variable in the Arduino code.
3. Copy the **Device ID** for `Grid Node Two` and paste it into the `NODE2_ID` variable.

### Step 3: Get Your Account Credentials
1. Navigate to **Credentials** on the left-hand menu in Sinric Pro.
2. Copy your **App Key** and paste it into the `APP_KEY` variable in the code.
3. Copy your **App Secret** and paste it into the `APP_SECRET` variable in the code.

*Once the code is uploaded to the Arduino, you can open the Alexa or Google Home app on your phone, link the "Sinric Pro" skill, and your grid nodes will automatically appear!*

---

## 🧠 Flashing Custom Edge-AI Firmware (VC-02)

Out of the box, the Ai-Thinker VC-02 comes with generic factory firmware. To make it communicate properly with the Cyber-Grid's edge-detection logic and internal pull-up resistors, you must flash it with custom firmware.

### Step 1: Build the Firmware on the Portal
1. Navigate to the [Ai-Thinker Smart Voice Platform](http://voice.ai-thinker.com/#/) and create a new project for the **VC-02** chip.
2. **Define the Wake Word:** Set your custom activation phrase under Vocabulary (e.g., *"Hello Cyber Grid"*).
3. **Map the Hardware Pins (Critical):**
   * Go to the **Control Details** tab.
   * Command 1 (*"Turn on Grid One"*): Set action to control **`GPIO_B2`** -> Parameter: **`low level trigger`** (Pulse, ~500ms).
   * Command 2 (*"Turn on Grid Two"*): Set action to control **`GPIO_A27`** -> Parameter: **`low level trigger`** (Pulse, ~500ms).
   * *Note: Using a "low level" trigger is absolutely mandatory to trigger the Arduino's `INPUT_PULLUP` safely without getting the relays stuck in a permanent loop.*
4. Click **Build** at the bottom of the page and download the generated `.bin` firmware file.

### Step 2: Burner Tool Usage

*(Note: Ensure the VC-02 module is completely disconnected from the Arduino before flashing.)*

#### 1. Use the UniOneUpdateTool.exe [Application File] to open the tool

<img width="1607" height="240" alt="Screenshot 2026-04-12 214723" src="https://github.com/user-attachments/assets/3350b3f2-f85b-4f29-b2b6-f3e935aa291c" />

#### 2. Select the COM Port of your device and upload the uni_app_release_update.bin file into the software
*(Note: The Red underlined is the COM Port | The Blue lined is the Browse Option | The Yellow lined is the Upload Option. To Upload the firmware press the RST Button on VC-02 and then hit upload)*

<img width="836" height="421" alt="Screenshot 2026-04-12 215021" src="https://github.com/user-attachments/assets/9fdfe059-f22a-4f25-a9f2-2410ca812589" />

---

## 🚀 Installation & Setup

1. **Arduino Libraries:** Ensure `PZEM004Tv40`, `U8g2`, `NTPClient`, and `SinricPro` are installed via the Arduino Library Manager.
2. **Network Configuration:** Update the `ssid` and `pass` variables with your local Wi-Fi credentials in the code.
3. **Flash the Board:** Upload the master build to your Arduino UNO R4 WiFi.

## 🌐 Usage & Interface
Once booted, the OLED screen will display the assigned IP address. Enter this IP into any web browser on the same network to access the Enterprise Dashboard. 
* **Offline Failsafe:** If your router fails, connect your device to the `Cyber-Grid-Offline` Wi-Fi network (Password: `12345678`) and navigate to `192.168.4.1`.

---

## 🕸️ Web Dashboard

Cyber-Grid features a responsive, mobile-first Web Interface hosted entirely on the Arduino UNO R4. Built from scratch with HTML, CSS, and vanilla JavaScript, the dashboard offers a dark-mode "enterprise" aesthetic with zero reliance on external cloud hosting for the UI.

### 💻 Desktop View

<img width="1919" height="605" alt="image" src="https://github.com/user-attachments/assets/14710aa5-ceb6-4d59-8e26-d88758785111" />

### 📱 Phone View

<p><img width="300" height="600" alt="image" src="https://github.com/user-attachments/assets/cf0ba5c1-3392-406b-998c-39dcc70ae4dd"/></p>
<p><img width="300" height="200" alt="image" src="https://github.com/user-attachments/assets/b5c24a21-162f-466c-b43e-b9eae7dccb23"/></p>

### Dashboard Capabilities:
* 📈 **Live Data Visualization:** Integrates `Chart.js` for real-time, animated graphing of total grid power draw (Watts).
* 💰 **Cost Projections:** Dynamically calculates current session cost and projects daily/monthly estimated bills based on your custom EEPROM-saved Tariff rate.
* ⏱️ **Visual SVG Timers:** Independent countdown rings for Node 1 and Node 2, allowing you to set auto-shutoff durations with a smooth visual interface.
* 🚦 **Auto-Limit Overrides:** Configure specific wattage safety limits directly from the UI. If a node exceeds the limit, the dashboard visually alerts you and hardware trips the relay.
* 📅 **RTC Scheduling:** Input specific ON and OFF times for both nodes, syncing with network time protocols (NTP) to automate your grid.
* 🍎 **Cross-Platform Rock Solid:** Engineered with strict HTTP headers and timestamped "cache-busting" `fetch()` requests, ensuring immediate button responsiveness and perfect layout rendering on strict operating systems like iOS/Safari.
 
---

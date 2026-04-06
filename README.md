<div align="center">

# AttendX

**RFID-based Attendance Management System**

*ESP32 · MFRC522 · SSD1306 OLED · DS1302 RTC · Built-in Web Dashboard*

![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B%20%2F%20Arduino-orange?style=flat-square)
![Version](https://img.shields.io/badge/version-1.0.0-black?style=flat-square)

</div>

---

# Gallary

![WhatsApp Image 2026-04-06 at 12 33 07](https://github.com/user-attachments/assets/e27773ff-f93f-43a5-b7fe-0db5beef9a45)

![WhatsApp Image 2026-04-06 at 12 34 46](https://github.com/user-attachments/assets/782ce9e5-394c-4a5d-9071-150c99c1ffb8)

---

## What is AttendX?

AttendX is a self-contained, battery-powered attendance system. Students tap an RFID card — the device logs the scan with a timestamp and makes the record instantly available on a built-in web dashboard accessible from any phone or laptop. No internet connection, no external server, no apps to install.

The ESP32 acts as its own Wi-Fi access point. Everything runs on the device.

---

## Features

- **RFID scanning** — ISO 14443-A cards via MFRC522
- **OLED display** — 9 display modes, yellow/blue SSD1306
- **Built-in web dashboard** — admin panel served directly from ESP32
- **Battery powered** — 3.7 V Li-ion + TP4056 USB-C charger
- **DS1302 RTC** — keeps time through power cuts (CR2032 backup)
- **Dual Wi-Fi** — standalone AP mode or connect to existing network
- **SPIFFS storage** — student roster and attendance records survive reboots
- **CSV export** — download attendance sheet from the dashboard
- **Real-time updates** — SSE push to admin panel on every scan
- **One-button navigation** — cycle OLED modes without touching a phone

---

## Hardware

### Bill of Materials

| Component | Qty | Notes |
|-----------|-----|-------|
| ESP32 Dev Board | 1 | Dual-core 240 MHz, 4 MB flash |
| MFRC522 RFID Module | 1 | 13.56 MHz, ISO 14443-A |
| SSD1306 OLED 0.96" | 1 | 128×64 px, I2C |
| DS1302 RTC Module | 1 | With CR2032 coin cell |
| CR2032 Coin Cell | 1 | RTC backup battery |
| TP4056 Charger Board | 1 | USB Type-C |
| Li-ion Battery 3.7 V | 1 | 18650 or flat LiPo |
| Tactile Button | 1 | OLED mode cycling |
| Power Switch | 1 | SPDT |
| 3D Printed Housing | 1 | 120 × 90 × 35 mm, 1.2 mm walls |

### Wiring

#### MFRC522 RFID → ESP32

| RFID Pin | ESP32 Pin |
|----------|-----------|
| 3.3 V | 3.3 V rail |
| GND | GND |
| RST | GPIO 4 |
| SDA (SS) | GPIO 5 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| SCK | GPIO 18 |
| IRQ | — (not connected) |

#### SSD1306 OLED → ESP32

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC | 3.3 V rail |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |

#### DS1302 RTC → ESP32

| RTC Pin | ESP32 Pin | Notes |
|---------|-----------|-------|
| VCC | 3.3 V rail | |
| GND | GND | |
| CLK | GPIO 17 (TX2) | Repurposed UART2 TX |
| DAT | GPIO 16 (RX2) | Repurposed UART2 RX |
| RST | GPIO 1 (TX0) | Repurposed UART0 TX — USB Serial disabled |

#### Other

| Signal | ESP32 Pin |
|--------|-----------|
| Button | GPIO 15 (INPUT_PULLUP) |
| Speaker | GPIO 2 |

> ⚠️ **All peripherals run on the 3.3 V rail. Do not connect anything to VIN/5 V.**  
> ⚠️ USB Serial (`Serial.print`) is **disabled** — GPIO1 (TX0) is used by the DS1302 RST line.

---

## Software

### Required Libraries

Install all of these from the Arduino Library Manager:

| Library | Purpose |
|---------|---------|
| `Adafruit GFX` | Graphics primitives |
| `Adafruit SSD1306` | OLED driver |
| `ArduinoJson` (v6) | JSON parsing / serialization |
| `ESPAsyncWebServer` | Async HTTP server |
| `AsyncTCP` | Dependency for ESPAsyncWebServer |
| `MFRC522` | RFID reader driver |
| `Rtc by Makuna` | DS1302 RTC driver (includes ThreeWire) |

### Arduino IDE Settings

| Setting | Value |
|---------|-------|
| Board | ESP32 Dev Module |
| Partition Scheme | **Default 4MB with spiffs** |
| Upload Speed | 921600 |

---

## Configuration

Edit these constants at the top of `main.ino` before flashing:

```cpp
const char *AP_SSID    = "AttendX";   // Wi-Fi hotspot name (open — no password)
const char *ADMIN_PASS = "1202";      // Web admin panel password
const long  GMT_OFFSET = 7200;        // UTC offset in seconds (UTC+2 = 7200)
const int   DST_OFFSET = 0;           // Daylight saving offset
const char *NTP_SERVER = "pool.ntp.org";
```

---

## Getting Started

### 1. Flash the firmware

1. Open `main.ino` in Arduino IDE
2. Select **ESP32 Dev Module** and set partition scheme to **Default 4MB with spiffs**
3. Set your `GMT_OFFSET` and `ADMIN_PASS`
4. Upload the sketch

### 2. Connect to AttendX

1. Power on the device — the OLED shows the ready screen after ~3 seconds
2. On your phone or laptop, connect to Wi-Fi: **`AttendX`** *(open network, no password)*
3. Open a browser and go to **`http://192.168.4.1`**
4. Log in with your admin password

### 3. Enroll students

1. Click **+ Add Student** in the dashboard
2. Enter name, student ID, and subject
3. Click **Next → Scan Card**
4. The student taps their RFID card to the reader
5. Done — student appears in the table

### 4. Take attendance

Students tap their card → OLED shows ✓ ACCESS GRANTED → dashboard updates live.

---

## Time Management

AttendX uses a **3-level time priority system**:

```
1. NTP sync (automatic, when connected to internet via STA mode)
        ↓ writes result to DS1302
2. DS1302 restore (automatic at boot, keeps time through power cuts)
        ↓ fallback if RTC battery is dead
3. Manual set (via web dashboard "Set Time" button or button menu)
        ↓ also writes to DS1302
```

In **AP-only mode** (no internet), the DS1302 maintains the clock as long as the CR2032 coin cell is alive. Set the time once from the admin dashboard and it persists indefinitely.

---

## OLED Display Modes

The physical button cycles through modes. Long-press (≥1 s) enters settings.

| Mode | Description | Trigger |
|------|-------------|---------|
| `MODE_READY` | Clock, date, scan prompt | Default / idle |
| `MODE_SCAN_RESULT` | ✓ / ✗ with student name | After card tap (auto-returns in 4 s) |
| `MODE_ENROLL` | "SCAN CARD — For: \<name\>" | Web admin → Add Student |
| `MODE_ATTENDED` | Count of present students | Button press |
| `MODE_ABSENT` | Count of absent students | Button press |
| `MODE_WEBLINK` | IP address for admin panel | Button press |
| `MODE_SETTINGS` | AP Only / AP+WiFi / Set Time | Long press |
| `MODE_SET_TIME` | Field-by-field time entry | From Settings |
| `MODE_RESTART` | "Settings saved, please restart" | After saving Wi-Fi |

---

## Web Dashboard

| Page | URL | Auth |
|------|-----|------|
| Admin Login | `/` | — |
| Admin Dashboard | `/admin` | ✅  Token |
| WiFi Setup | `/wifi-setup` | ✅ Token |
| Student Page | `/student` | — |

### API Endpoints

All `/api/` endpoints except `/api/login` require header `X-Token: <token>`.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/login` | Login, returns session token |
| `GET` | `/api/students/today` | Students + today's attendance |
| `POST` | `/api/enroll/start` | Begin card enrollment |
| `POST` | `/api/enroll/cancel` | Cancel enrollment |
| `DELETE` | `/api/students/:uid` | Remove student |
| `POST` | `/api/students/attend` | Mark student present |
| `POST` | `/api/students/unattend` | Mark student absent |
| `POST` | `/api/students/attend-all` | Mark all present |
| `POST` | `/api/students/absent-all` | Mark all absent |
| `POST` | `/api/settime` | Set device RTC from epoch timestamp |
| `GET` | `/api/wifi/scan` | Scan nearby Wi-Fi networks |
| `POST` | `/api/wifi` | Save Wi-Fi credentials |
| `POST` | `/api/reset` | Wipe all data |
| `GET` | `/events` | SSE stream for real-time scan events |

---

## Data Storage (SPIFFS)

| File | Content |
|------|---------|
| `/students.json` | Student roster (uid, name, studentId, subject) |
| `/attendance.json` | Scan log (uid, date, timestamp) |
| `/config.json` | Wi-Fi credentials and mode |

Only the **first scan per student per day** is recorded. Subsequent taps show "Already Scanned".

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| OLED blank | Check I2C address (0x3C or 0x3D). Verify SDA=21, SCL=22 |
| RFID not reading | Check SPI wiring. Confirm 3.3 V power only |
| Can't connect to AttendX | Network is open — no password needed. Try forgetting/re-joining |
| Wrong time after boot | DS1302 coin cell may be dead. Set time from admin dashboard to reprogram RTC |
| Time resets to 1970 | Replace CR2032 cell, then set time again |
| Students lost after restart | Partition scheme must be "Default 4MB with spiffs" |
| SSE not updating | Use Chrome or Firefox. Check browser console for EventSource errors |

---

## Project Team

| Name | Role |
|------|------|
| **Ahmed Barakat** | Team Leader / Embedded Developer |
| **Abdulrahman Yossef** | 3D Model / Housing Designer |
| **Menna Khattab** | Web Developer |
| **Hana Nasef** | Hardware Engineer |
| **Hana Elbirmawy** | Hardware Engineer |

---

<div align="center">

**AttendX** — RFID Attendance System &nbsp;·&nbsp; Version 1.0 &nbsp;·&nbsp; March 2026

</div>

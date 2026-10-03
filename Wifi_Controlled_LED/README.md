# ESP32 Wi-Fi Controlled RGB LED

A modern IoT project built with the **ESP-IDF framework** and **PlatformIO** that lets you wirelessly control an RGB LED via a web application hosted directly on the ESP32. 

The frontend is modularized into separate HTML, CSS, and JavaScript files and embedded into flash memory at compile time—delivering a dark-mode mobile-friendly UI without needing an external SD card or SPIFFS filesystem.

---

## Features

- **Embedded HTTP Web Server**: Powered by the native `esp_http_server` component running on port 80.
- **Modular Frontend Architecture**: Clean separation of concerns with standalone files in [`web/`](./web):
  - [`index.html`](./web/index.html): Semantic HTML structure.
  - [`style.css`](./web/style.css): Modern dark-mode responsive design optimized for mobile and desktop.
  - [`app.js`](./web/app.js): Asynchronous JavaScript sending non-blocking `fetch()` requests.
- **Zero-Filesystem Binary Embedding**: Web assets are compiled directly into the firmware flash binary using `EMBED_TXTFILES` / `board_build.embed_txtfiles`.
- **Live Debugging & Telemetry**:
  - Comprehensive ESP-IDF serial logs via `esp_log` for hardware, Wi-Fi, and HTTP events.
  - An on-screen live debug console inside the web card displaying HTTP status codes and round-trip responses.
  - Global client error trapping (`window.onerror`) for quick diagnostics on mobile devices.
- **Common Anode & Common Cathode Support**: Seamless software-level polarity inversion via a single configuration flag.
- **Auto-Reconnection**: Resilient Wi-Fi event handler that automatically reconnects if the signal drops.

---

## Hardware Requirements & Pinout

### Components
* ESP32 Development Board (e.g., ESP32-WROOM-32 / NodeMCU-32S)
* 1x RGB LED (Common Cathode or Common Anode)
* 3x Resistors (220Ω – 330Ω recommended for current limiting)
* Jumper wires & breadboard

### Wiring Diagram

| RGB LED Pin | Current Limiting Resistor | ESP32 GPIO Pin |
| :--- | :--- | :--- |
| **Red Channel** | 220Ω | **GPIO 27** |
| **Green Channel** | 220Ω | **GPIO 25** |
| **Blue Channel** | 220Ω | **GPIO 32** |
| **Common Pin** | None | **GND** *(if Common Cathode)* OR **3.3V** *(if Common Anode)* |

> **Note:** If you want to use different pins, update `RED_GPIO`, `GREEN_GPIO`, and `BLUE_GPIO` in [`src/main.c`](./src/main.c).

---

## Project Structure

```text
Wifi_Controlled_LED/
├── web/                     # Frontend web application assets
│   ├── index.html           # Main user interface
│   ├── style.css            # Dark UI stylesheet & button styles
│   └── app.js               # Client-side API request handler & on-screen logs
├── src/
│   ├── CMakeLists.txt       # ESP-IDF component CMake registration
│   └── main.c               # Core firmware (GPIO, Wi-Fi STA, HTTP handlers)
├── platformio.ini           # PlatformIO project configuration & asset embed rules
├── CMakeLists.txt           # Root ESP-IDF CMake project definition
└── README.md                # Project documentation
```

---

## Configuration

Open [`src/main.c`](./src/main.c) and configure your Wi-Fi credentials and LED type:

```c
// 1. Enter your 2.4 GHz Wi-Fi credentials
#define WIFI_SSID     "Your_WiFi_Network"
#define WIFI_PASS     "Your_WiFi_Password"

// 2. Configure your RGB LED type
#define COMMON_ANODE  false  // 'false' for Common Cathode (-), 'true' for Common Anode (+)
```

---

## Build and Flash

### Using PlatformIO (VS Code Extension)
1. Open this project folder in **VS Code**.
2. Click the **PlatformIO** icon on the sidebar.
3. Under **Project Tasks** > **esp32dev**:
   - Click **Build** to compile the firmware and bundle the web assets.
   - Connect your ESP32 via USB and click **Upload**.
   - Click **Monitor** to open the Serial Monitor (configured for 115200 baud).

### Using PlatformIO CLI
```bash
# Compile firmware
pio run

# Upload to ESP32
pio run --target upload

# Open Serial Monitor
pio device monitor -b 115200
```

---

## Usage

1. Open the Serial Monitor after flashing.
2. Once connected to your Wi-Fi, the ESP32 will output its assigned IP address:
   ```text
   I (2450) ESP32_GPIO_RGB: Wi-Fi Connected! IP Address: 192.168.1.45
   I (2455) ESP32_GPIO_RGB: Starting HTTP server on port 80...
   ```
3. Open any web browser on a device connected to the **same Wi-Fi network** and navigate to:
   ```text
   http://192.168.1.45/
   ```
   *(Replace with your ESP32's actual IP address)*
4. Click the buttons (**Red**, **Green**, **Blue**, or **Turn OFF**) to control the LED and watch live HTTP debug logs update on both the webpage and serial monitor.

---

## REST API Reference

The onboard HTTP server exposes the following endpoints:

| Method | Endpoint | Description | Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/` or `/index.html` | Serves the HTML user interface (`text/html`) | HTML Document |
| `GET` | `/style.css` | Serves the CSS stylesheet (`text/css`) | CSS Content |
| `GET` | `/app.js` | Serves the client JavaScript (`application/javascript`) | JS Content |
| `GET` | `/setgpio?r={0\|1}&g={0\|1}&b={0\|1}` | Sets the digital outputs of the RGB LED channels | `OK` (200) or `404` |

### Example API Request
To turn on Green only:
```http
GET /setgpio?r=0&g=1&b=0 HTTP/1.1
Host: 192.168.1.45
```
Response:
```http
HTTP/1.1 200 OK
Content-Type: text/plain
Access-Control-Allow-Origin: *

OK
```

---

## License

This project is open-source and free to use for DIY, educational, and commercial applications.

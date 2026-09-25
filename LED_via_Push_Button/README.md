# ESP32 Push Button LED Controller

A clean, efficient, and event-driven ESP32 firmware project built with the **ESP-IDF** framework on **PlatformIO**. It controls an LED via a push button using **hardware interrupts (GPIO ISR)** and **FreeRTOS task notifications**, eliminating redundant CPU looping and achieving **0% idle CPU utilization**.

---

## Features

- **Hardware Interrupt-Driven**: Senses button presses via falling-edge GPIO interrupts (`GPIO_INTR_NEGEDGE`) rather than continuous polling.
- **Zero Redundant Looping (0% Idle CPU)**: The worker task sleeps indefinitely with `portMAX_DELAY` until notified by the hardware ISR.
- **Bounce Storm Prevention**: Disables the GPIO interrupt inside the ISR and re-enables it only after debouncing, completely preventing interrupt storms from mechanical switch chatter.
- **Toggle Logic**: Each press of the momentary push button flips the LED state (`OFF` $\rightarrow$ `ON` $\rightarrow$ `OFF`).
- **Hold-Down Guard**: Waits for the button to be released before allowing the next toggle, preventing rapid flickering while held.
- **Internal Pull-up**: Uses the ESP32's built-in pull-up resistor on the button pin—no external pull-up resistor is required.
- **Structured Serial Logging**: Outputs timestamped system logs at `115200` baud using `ESP_LOGI`.

---

## Hardware Requirements

| Component | Quantity | Notes |
| :--- | :--- | :--- |
| **ESP32 Development Board** | 1 | NodeMCU-32S, ESP32-WROOM-32, etc. |
| **Push Button** | 1 | Momentary tactile switch (2-pin or 4-pin) |
| **LED** | 1 | 3mm or 5mm LED (any color) |
| **Resistor** | 1 | 220Ω to 330Ω (current limiter for LED) |
| **Breadboard & Jumpers** | - | For making connections |

---

## Circuit Diagram & Wiring

### Schematic Diagram

```text
                           ESP32 Dev Board
                     +-------------------------+
                     |                         |
                     |                  GPIO 25|----[ 220Ω Resistor ]----( + LED Anode )
                     |                         |                         ( - LED Cathode )
                     |                      GND|--------------------------------|
                     |                         |
                     |                  GPIO 32|----------------\
                     |                         |                 | [ Push Button ]
                     |                      GND|----------------/
                     +-------------------------+
```

### Connection Summary Table

| Peripheral | Component Terminal | ESP32 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **LED** | Long Leg (Anode `+`) | **GPIO 25** | Connect in series with 220Ω–330Ω resistor |
| **LED** | Short Leg (Cathode `-` / flat edge) | **GND** | Return path to Ground |
| **Push Button** | Terminal A | **GPIO 32** | Sensed as Active-LOW |
| **Push Button** | Terminal B | **GND** | Pulled to 0V on press |

> [!TIP]
> **4-Pin Tactile Button Tip**:
> Standard 4-pin tactile buttons have opposite pins permanently connected in pairs internally. To prevent accidentally shorting GPIO 32 to GND:
> - Connect **GPIO 32 to Pin 1** (top-left)
> - Connect **GND to Pin 4** (bottom-right)
> 
> Wiring **diagonally** guarantees that the connection is only made when the button is physically pressed down.

---

## Important Technical Note: RTC GPIOs & Pull-ups

In this project, the push button is connected to **GPIO 32**.

On the ESP32, GPIO 32 is also an **RTC GPIO** (`RTCIO_CHANNEL_9`). In the ESP-IDF driver:
1. Calling `gpio_reset_pin(GPIO_NUM_32)` invokes `rtc_gpio_deinit()`, which disables the RTC pull-up resistor.
2. Therefore, the internal pull-up must be explicitly enabled using `gpio_pullup_en(BUTTON_PIN)`.

The bulletproof initialization pattern used in this project:

```c
gpio_reset_pin(BUTTON_PIN);
gpio_pullup_en(BUTTON_PIN);
gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
```

---

## Project Structure

```text
LED_via_Push_Button/
├── include/
├── lib/
├── src/
│   ├── CMakeLists.txt      # ESP-IDF component registration
│   └── main.c              # Main application source code
├── test/
├── platformio.ini          # PlatformIO configuration (board, framework, monitor_speed)
└── README.md               # Project documentation
```

---

## Getting Started

### 1. Prerequisites
- [VS Code](https://code.visualstudio.com/)
- [PlatformIO IDE Extension](https://platformio.org/platformio-ide) for VS Code

### 2. Build and Flash
1. Open the `LED_via_Push_Button` folder in VS Code.
2. Connect your ESP32 board to your computer via USB.
3. In PlatformIO:
   - Click **Build** (checkmark icon in the status bar) to compile the firmware.
   - Click **Upload** (arrow icon in the status bar) to flash the firmware onto the ESP32.
4. Click **Serial Monitor** (plug icon in the status bar) to view logs at `115200` baud.

---

## Serial Monitor Output

When running, you should see logs similar to:

```text
I (312) BUTTON_LED: ESP32 Button-controlled LED initialized.
I (312) BUTTON_LED: LED Pin: GPIO 25 | Button Pin: GPIO 32
I (312) BUTTON_LED: Initial LED State: OFF
I (2450) BUTTON_LED: Button pressed! LED is now ON
I (4120) BUTTON_LED: Button pressed! LED is now OFF
```


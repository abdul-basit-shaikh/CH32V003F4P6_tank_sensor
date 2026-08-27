# 💧 Hardware Components & BOM List — Tank Water Level Sensor
**Project:** Battery-Powered Wireless Tank Sensor  
**Microcontroller:** CH32V003F4P6 (RISC-V 32-bit, TSSOP-20)  
**Wireless:** SX1278 LoRa Ra-02 (433 MHz)  
**Power Source:** 2x AA (3.0V Direct) ya 4x AA (6.0V + 3.3V LDO)  
**Excel File:** [Tank_Sensor_Hardware_List.xlsx](file:///c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Tank_Sensor_Hardware_List.xlsx)

---

## 📋 Complete Bill of Materials (BOM) & Robu.in Links

Neeche har ek component ki detail, part number, package, robu.in link aur uska exact use (purpose) diya gaya hai:

| # | Ref | Component Name | Value / Spec | Package | Qty | Robu.in Link | Kis Liye Lagta Hai (Exact Purpose) |
|:---:|:---:|:---|:---|:---|:---:|:---:|:---|
| 1 | **U1** | **Main Microcontroller (MCU)** | **CH32V003F4P6** | TSSOP-20 | 1 | [🔗 Search Robu.in](https://robu.in/?s=CH32V003&post_type=product) | **Poore sensor ka brain.** Probes read karta hai, deep sleep manage karta hai, battery calculate karta hai, aur LoRa se controller ko data bhejta hai. |
| 2 | **U2** | **Wireless LoRa Module** | **SX1278 Ra-02 (Ai-Thinker)** | SMD-16 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=SX1278+LoRa+Module+Ra-02&post_type=product) | **433MHz Long Range Radio.** Tanki se controller tak 500m - 1km+ range me paani ke level aur battery ka data wirelessly transmit karta hai. |
| 3 | **Q6** | **P-Channel MOSFET** | **AO3401A / SI2301** (30V, 4.2A) | SOT-23 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=AO3401A+MOSFET&post_type=product) | **Anti-Corrosion High-Side Switch.** Paani ke common wire me 24/7 current nahi bhejta. Jab MCU reading leta hai sirf 10ms ke liye VCC pulse deta hai, baki waqt paani me 0V rehta hai taaki probes rust/electrolysis se kharab na hon. |
| 4 | **Q1** | **NPN Transistor (25% Probe)** | **BC547 / 2N3904** | TO-92 / SOT-23 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=BC547+Transistor&post_type=product) | **25% Water Level Buffer.** Paani 25% probe ko touch karta hai toh base trigger hokar Collector (PC0) ko LOW (0V) pull karta hai. |
| 5 | **Q2** | **NPN Transistor (50% Probe)** | **BC547 / 2N3904** | TO-92 / SOT-23 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=BC547+Transistor&post_type=product) | **50% Water Level Buffer.** 50% level touch hone par Collector (PC1) ko LOW pull karta hai. |
| 6 | **Q3** | **NPN Transistor (75% Probe)** | **BC547 / 2N3904** | TO-92 / SOT-23 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=BC547+Transistor&post_type=product) | **75% Water Level Buffer.** 75% level touch hone par Collector (PC2) ko LOW pull karta hai. |
| 7 | **Q4** | **NPN Transistor (100% Probe)** | **BC547 / 2N3904** | TO-92 / SOT-23 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=BC547+Transistor&post_type=product) | **100% Water Level Buffer.** 100% level touch hone par Collector (PC4) ko LOW pull karta hai. |
| 8 | **Q5** | **P-Channel MOSFET (Option B)** | **AO3401A** | SOT-23 | 1 *(Opt B)* | [🔗 Buy on Robu.in](https://robu.in/?s=AO3401A+MOSFET&post_type=product) | **Zero-Drop Reverse Battery Protection.** 2x AA direct mode me battery ulti lagne par circuit ko instantly disconnect karta hai (Sirf 0.004V drop). |
| 9 | **U3** | **Ultra-Low $I_q$ 3.3V LDO (Option A)** | **HT7333-2 / XC6206P332MR** | SOT-89 / SOT-23 | 1 *(Opt A)* | [🔗 Buy on Robu.in](https://robu.in/?s=HT7333&post_type=product) | **Voltage Regulator.** 4x AA (6V) battery setup me voltage ko solid 3.3V pe regulate karta hai (Quiescent current < 3µA). |
| 10 | **D2** | **Step-down & Reverse Diode (Option A)**| **1N4007 / M7** | DO-41 / SMA | 1 *(Opt A)* | [🔗 Buy on Robu.in](https://robu.in/?s=1N4007+Diode&post_type=product) | **LDO Protection.** 4x AA naye cells (6.4V) ko 0.7V kam karke 5.7V safe banata hai aur reverse polarity rokta hai. |
| 11 | **F1** | **Resettable PTC Fuse** | **500mA (Bourns / Littelfuse)** | 1206 SMD | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=1206+PTC+Fuse+500mA&post_type=product) | **Short Circuit Protection.** Agar battery terminals ya board me short circuit ho toh battery ko blast ya drain hone se bachata hai. |
| 12 | **C1** | **Main Bulk Reservoir Capacitor** | **100µF 16V / 25V** | Radial / 1206 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100uF+Capacitor&post_type=product) | **Energy Reservoir.** Battery ke paas lagta hai. Jab LoRa module TX ke waqt sudden 120mA current spike leta hai, ye voltage drop nahi hone deta. |
| 13 | **C2** | **LoRa Bulk Capacitor** | **10µF 10V Ceramic** | 0805 SMD | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=10uF+0805+SMD+Capacitor&post_type=product) | **LoRa Local Power Buffer.** LoRa VCC/GND ke paas lagta hai for local transient response. |
| 14 | **C3** | **LoRa RF Decoupling Capacitor** | **100nF (104) Ceramic** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100nF+0805+SMD+Capacitor&post_type=product) | **High-Frequency Noise Filter.** LoRa module ke digital switching aur SPI clock noise ko ground karta hai. |
| 15 | **C4** | **MCU Decoupling Capacitor** | **100nF (104) Ceramic** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100nF+0805+SMD+Capacitor&post_type=product) | **MCU Power Filter.** CH32V003 MCU ke VDD (Pin 9) aur VSS (Pin 7) ke 3mm paas lagta hai for clean MCU clock. |
| 16 | **C5** | **MCU Reset Capacitor** | **100nF (104) Ceramic** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100nF+0805+SMD+Capacitor&post_type=product) | **Clean Power-On Reset.** NRST pin ko power-on par clean reset pulse deta hai aur false resets rokta hai. |
| 17 | **C6-C9**| **Probe Input Filter Capacitors** | **4x 100nF (104) Ceramic** | 0603 / 0805 | 4 | [🔗 Buy on Robu.in](https://robu.in/?s=100nF+0805+SMD+Capacitor&post_type=product) | **Noise / ESD Filter.** Tanki ke 25%, 50%, 75%, 100% lambe taaron se aane wale static, RF noise aur false triggering ko ground karte hain. |
| 18 | **C10** | **Common Line Filter Capacitor** | **100nF (104) Ceramic** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100nF+0805+SMD+Capacitor&post_type=product) | **Common Line Protection.** Common sensor line ko ESD aur static spike se bachata hai. |
| 19 | **R3** | **NRST Pull-up Resistor** | **10kΩ (0.125W)** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=10k+0805+SMD+Resistor&post_type=product) | **Reset Pin Stabilizer.** CH32V003 ke NRST (Pin 4) ko VCC se pull-up rakhta hai taaki chip hung na ho. |
| 20 | **R4** | **LED Current Limiter Resistor** | **1kΩ (0.125W)** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=1k+0805+SMD+Resistor&post_type=product) | **LED Protection.** Status LED (PD2) ko safe 1-2mA current deta hai taaki battery waste na ho. |
| 21 | **R5-R8**| **Probe Base Resistors** | **4x 100kΩ (0.125W)** | 0603 / 0805 | 4 | [🔗 Buy on Robu.in](https://robu.in/?s=100k+0805+SMD+Resistor&post_type=product) | **Transistor Base Protection.** Q1-Q4 BC547 ke base me safe micro-ampere current allow karte hain jab paani touch hota hai. |
| 22 | **R9** | **MOSFET Gate Pull-up Resistor** | **100kΩ (0.125W)** | 0603 / 0805 | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=100k+0805+SMD+Resistor&post_type=product) | **Q6 Gate Lock.** PD3 floating hone par ya deep sleep ke waqt Q6 Gate ko VCC se pull karke MOSFET ko 100% OFF rakhta hai. |
| 23 | **D1** | **Status Indicator LED** | **0805 SMD / 3mm Green LED** | SMD / TH | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=Green+LED+0805&post_type=product) | **User Feedback.** Pairing mode, reset indication, aur data transmission confirmation blink karne ke liye. |
| 24 | **SW1** | **User Push Button** | **6x6mm Tactile Switch** | SMD / TH | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=Tactile+Push+Button+6x6&post_type=product) | **Pairing & Reset.** 3s hold = Reboot, 5s hold = Controller ke saath Pairing Mode. |
| 25 | **SW2** | **Power Slide Switch** | **SPST Slide Switch 3-Pin** | Through-Hole | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=SPST+Slide+Switch&post_type=product) | **Battery ON/OFF Switch.** Board ko storage ya maintenance ke waqt physically switch off karne ke liye. |
| 26 | **J1** | **Battery Connector** | **2-Pin JST-XH / 2.54mm Screw** | Through-Hole | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=JST+XH+2+Pin+Connector&post_type=product) | Battery pack (2x AA ya 4x AA holder) connect karne ke liye. |
| 27 | **J2** | **SWD Programming Header** | **1x3 Male Header (2.54mm)** | Through-Hole | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=2.54mm+Male+Berg+Strip&post_type=product) | **Firmware Flashing.** WCH-LinkE programmer se code upload karne ke liye (VCC, SWIO/PD1, GND). |
| 28 | **J3** | **Tank Sensor Connector** | **1x5 Screw Terminal Block (3.81mm / 5.08mm)** | Through-Hole | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=5+Pin+Screw+Terminal+Block&post_type=product) | Tanki ke 5 taar connect karne ke liye: Common (VCC), 25%, 50%, 75%, 100%. |
| 29 | **J4** | **Debug UART Header (Optional)** | **1x2 Male Header (2.54mm)** | Through-Hole | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=2.54mm+Male+Berg+Strip&post_type=product) | Serial monitor debugging ke liye (PD5 TX + GND). |
| 30 | **ANT1**| **433MHz LoRa Antenna** | **Spring Coil Antenna / IPEX SMA** | 433 MHz | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=433MHz+Spring+Antenna&post_type=product) | LoRa signal transmit karne ke liye. (Spring antenna PCB par solder hoti hai). |
| 31 | **PROBES**| **Water Level Probes** | **SS304 / SS316 Stainless Steel Screws** | M3/M4 Bolt | 5 | [🔗 Buy on Robu.in](https://robu.in/?s=Stainless+Steel+Bolt+M3&post_type=product) | Paani ke andar daalne ke liye probes (Rust-proof). |
| 32 | **PROG1**| **WCH Hardware Programmer** | **WCH-LinkE Debugger/Programmer** | USB Tool | 1 | [🔗 Buy on Robu.in](https://robu.in/?s=WCH-LinkE+Programmer&post_type=product) | CH32V003 MCU par firmware flash aur debug karne ke liye official tool. |

---

## 📌 CH32V003F4P6 TSSOP-20 Pin Connection Summary

| TSSOP-20 Pin | Name | Connected Component | Function & Direction |
|:---:|:---:|:---|:---|
| **Pin 1** | **PD4** | SX1278 LoRa RST | Output — LoRa Module Reset |
| **Pin 2** | **PD5** | J4 Debug UART TX | Output — 115200 Baud Serial Debug (Optional) |
| **Pin 3** | **PD6** | SW1 Push Button | Input (Pull-Up) — Pairing / Reset Button (Active LOW) |
| **Pin 4** | **PD7** | R3 (10k) + C5 (104) | NRST — Hardware Reset Pin |
| **Pin 5** | **PA1** | NC (Internal Vref used) | Spare (Battery 1.2V Internal Bandgap se 0µA drain pe measure hoti hai) |
| **Pin 6** | **PA2** | NC | Spare GPIO |
| **Pin 7** | **VSS** | Ground (GND Plane) | Power Ground |
| **Pin 8** | **PD0** | NC | Spare GPIO |
| **Pin 9** | **VDD** | VCC 3.0V/3.3V + C4 (104) | Power VDD (3.0V - 3.3V) |
| **Pin 10** | **PC0** | Q1 Collector (25% Probe) | Input (Pull-Up) — 25% Water Level Sense |
| **Pin 11** | **PC1** | Q2 Collector (50% Probe) | Input (Pull-Up) — 50% Water Level Sense |
| **Pin 12** | **PC2** | Q3 Collector (75% Probe) | Input (Pull-Up) — 75% Water Level Sense |
| **Pin 13** | **PC3** | SX1278 LoRa NSS (CS) | Output — SPI Chip Select |
| **Pin 14** | **PC4** | Q4 Collector (100% Probe) | Input (Pull-Up) — 100% Water Level Sense |
| **Pin 15** | **PC5** | SX1278 LoRa SCK | AF Output — SPI1 Clock |
| **Pin 16** | **PC6** | SX1278 LoRa MOSI | AF Output — SPI1 Master Out |
| **Pin 17** | **PC7** | SX1278 LoRa MISO | Input (Pull-Up) — SPI1 Master In |
| **Pin 18** | **PD1** | J2 SWIO Pin | Bidirectional — Single-Wire SWD Debug/Flash |
| **Pin 19** | **PD2** | D1 LED via R4 (1k) | Output — Status LED Indicator (Active HIGH) |
| **Pin 20** | **PD3** | Q6 P-MOSFET Gate via R9 | Output — Water Common Wire Pulsed Switch (Active LOW) |

---

## ⚡ Power Supply Choice Comparison

### 🟢 Option A: 4x AA Battery Setup (Recommended for 5 to 7 Years Life)
- **Parts:** 4x AA Holder + PTC Fuse 500mA + 1N4007 Diode + HT7333-2 3.3V LDO + 100µF Bulk Cap.
- **Advantage:** Poori battery life me solid 3.3V milta hai, LoRa peak power hamesha maximum range deti hai.

### 🔵 Option B: 2x AA Battery Setup (Recommended for Small Size & 2 to 3 Years Life)
- **Parts:** 2x AA Holder + PTC Fuse 500mA + AO3401A P-MOSFET (Q5) + 100µF Bulk Cap.
- **Advantage:** Koi LDO IC nahi chahiye, direct 3.0V battery se MCU aur LoRa chalte hain (Zero idle power waste).

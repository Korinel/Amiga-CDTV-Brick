# CDTV IR Controller - Release Notes

## Version 0.3
**Release Date:** [2025-11-08]

### Bug Fixes
- **Critical Fix: IR LED Polarity Correction**
  - Corrected IR LED (TSAL6200) orientation in both KiCad schematic symbol and PCB footprint
  - The LED symbol was incorrectly drawn with anode and cathode reversed, causing the component to be installed backwards
  - PCB now matches the TSAL6200 datasheet pinout (Pin 1 = Cathode, Pin 2 = Anode)
  - **Impact:** Previous versions (v0.1 and v0.2) required the IR LED to be installed with reversed polarity to function correctly. Version 0.3 allows correct installation following component markings (flat edge = cathode)

### Acknowledgements
Special thanks to **chrisblown** and **youenchene** for identifying and reporting this critical polarity issue! 

---

## Version 0.2
**Release Date:** [2025-07-18]



### Features Added
- **SW1 Power Switch:** Added a power switch to save on battery power
- **Pico Debug Header:** Exposed TX, RX, and GND pins on Raspberry Pi Pico for serial debugging and programming access

---

## Version 0.1
**Release Date:** [2025-05-15]

### Initial Release
- Original CDTV IR Controller design
- Raspberry Pi Pico-based IR transmitter
- TSAL6200 IR LED with IRLB8721 MOSFET driver circuit
- Basic power management circuitry

---

## Migration Notes

### Upgrading from v0.1 or v0.2 to v0.3
If you have already built a board using v0.1 or v0.2 gerbers:
- Your board will continue to work with the IR LED installed in reverse (opposite to component markings)
- No hardware modifications are required for existing boards
- Future builds using v0.3 gerbers should install the LED following standard component markings (flat edge toward MOSFET)

### Assembly Notes for v0.3
- Install TSAL6200 IR LED with the **flat edge of the dome toward the MOSFET** (cathode side)
- The square pad connects to the cathode (flat edge side)
- The round pad connects to the anode (round edge side)
# Embedded Smart Expense Manager — ESP32 + I²C

Enterprise-style embedded expense ledger simulator for Wokwi.

## Features

- ESP32 firmware with I²C LCD1602 and DS1307 RTC
- Categorized transaction inputs: MEAL ₹100, TRAVEL ₹200, BILLS ₹500
- CRC16-protected EEPROM journal with eight rotating records
- Recovery of the newest valid record after restart
- RTC timestamped serial audit events
- Input debouncing and protected two-second reset
- Budget state machine: NORMAL, WARNING at 80%, CRITICAL at 100%
- Green/yellow/red status LEDs and buzzer alerts
- Three LCD status pages: spend, budget remaining, and last transaction

## Run in Wokwi

Open https://wokwi.com/projects/466727957249717249, press **Start Simulation**, and open the Serial Monitor at 115200 baud.

Press the colored buttons to add expenses. Hold **RESET HOLD** for two seconds to clear the transaction ledger while preserving the budget. The LCD rotates through its status pages automatically.

This is a simulator-ready embedded prototype. Production deployment would additionally require authenticated users, encrypted/cloud persistence, a secure transaction API, and formal security testing.

# 🚗 ESP32 Dual-Core Automotive OBD-II Telematics Gateway & ECU Simulator

[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B)
[![RTOS: FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange.svg)](https://www.freertos.org/)
[![Hardware: ESP32](https://img.shields.io/badge/Hardware-ESP32%20WROOM--32-red.svg)](https://www.espressif.com/)
[![Protocol: OBD-II / V2X](https://img.shields.io/badge/Protocol-OBD--II%20%7C%20V2X-green.svg)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

An embedded vehicular telematics gateway and Powertrain Engine Control Unit (ECU) simulator built on an ESP32 microcontroller using **FreeRTOS** dual-core task scheduling, digital signal filtering, and real-time on-board diagnostic (OBD-II) state machines.

---

## 📌 Executive Summary & Architectural Motivation

In modern automotive embedded systems (such as Connected Cars and V2X nodes), microcontrollers cannot afford CPU blocking while serving high-latency wireless diagnostic requests. A missed throttle or brake sensor read due to networking overhead can violate strict safety-critical deadlines.

This project addresses this constraint by decoupling **safety-critical sensor acquisition** from **telematics communication** across the dual physical cores of the Xtensa LX6 microprocessor:
* **Core 1 (Safety Domain):** High-frequency (66Hz) ADC sampling of the Accelerator Pedal Sensor (APS) with digital low-pass filtering and fault-code evaluation.
* **Core 0 (Telematics Domain):** Low-priority wireless access point hosting an embedded web dashboard and asynchronous JSON streaming endpoint.

---

## 🏗️ System Architecture


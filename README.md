# uxdtdrv

`uxdtdrv` is an experimental Windows kernel-mode driver designed for Ultrasonic Tracking (UXDT). It was developed and utilized during a scientific evaluation of UXDT. The driver directly interfaces with system audio hardware using Kernel Streaming (KS) to capture microphone input and performs real-time Digital Signal Processing (DSP) to decode Frequency-Shift Keying (FSK) messages.

## Core Architecture

The driver is split into various operational components to handle low-level audio routing and signal processing:

* **Audio Device Initialization:** The driver queries system device interfaces to locate a suitable capture pin supporting 44100 Hz, 16-bit, stereo PCM audio.


* **DMA Ring Buffers:** It queries the WaveRT hardware latency and DMA buffer properties, spawning a dedicated system worker thread to continuously read physical memory buffers into a 6MB shared paged pool ring buffer.


* **Signal Demodulation:** A secondary worker thread analyzes the PCM stream. It utilizes a Goertzel algorithm filter to calculate the magnitude squared of target frequencies, determining whether a given 20ms window represents a binary `1` or `0`.


* **Payload Extraction:** Once a specific 16-bit preamble is detected, the driver decodes a 16-bit CRC (CRC16-IBM) and extracts the ASCII payload, verifying checksum integrity before passing it to user space.



## Technical Specifications

The DSP engine is hardcoded to the following FSK parameters:

* **Sample Rate:** 44100 Hz (Stereo, 16-bit).


* **Space Frequency (Bit 0):** 1000.0 Hz.


* **Mark Frequency (Bit 1):** 2000.0 Hz.


* **Bit Duration:** 20 ms.


* **Preamble:** `0b1111000011110000`.


* **Checksum:** CRC16-IBM (Polynomial `0x8005`).



## User-Mode Interface

User-mode applications interact with the driver by opening a handle to `\Device\UXDTDRV`.

* **`IOCTL_UT_RECEIVE`**: Accepts a `UT_RECEIVE` structure containing target frequencies, buffer lengths, and a notification event handle. The driver maps the receive buffer directly into the calling user-mode process. When a valid UXDT message is decoded, the driver populates this mapped memory and signals the user-mode event.


---

> **Note:** This README was created with the help of AI, but has been manually reviewed and adjusted for accuracy.

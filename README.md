# FLASHTICKER 7"

<p align="center">
  <img src="pictures/Flashman.png" alt="Flashman Logo" width="120" />
</p>

<p align="center">
  <strong>Bitcoin · Gold · Silber Preisticker für das Elecrow CrowPanel 7.0"</strong><br/>
  ESP32-S3 · LovyanGFX · LVGL 8 · Touch · WiFiManager
</p>

<p align="center">
  <a href="https://flashmanbTC.github.io/flashticker_7zoll/">
    <img src="https://img.shields.io/badge/Web%20Installer-v2.0-F7931A?style=for-the-badge&logo=espressif" alt="Web Installer" />
  </a>
  &nbsp;
  <img src="https://img.shields.io/badge/Hardware-ESP32--S3-blue?style=for-the-badge" alt="ESP32-S3" />
  &nbsp;
  <img src="https://img.shields.io/badge/Display-800×480-green?style=for-the-badge" alt="800x480" />
</p>

---

## Features

- **BTC / Gold / Silber** live in **USD · EUR · CHF**
- Gold & Silber wahlweise in **oz oder Gramm**
- **24h Kursänderung** in % angezeigt
- **Linien-Chart** mit 1T / 1W / 1M Perioden (kein Lag – alles beim Boot vorgeladen)
- **Vollbild-Modus** für die Top-Card: 72pt Preis, 24h High/Low aus Chart-Daten
- **Touch** zum Wechsel von Währung und Asset
- **WiFiManager** Captive-Portal mit **QR-Code** zur einfachen WLAN-Einrichtung
- **NTP Uhrzeit** + Datum (Zeitzone konfigurierbar)
- Eigene REST-API: [ticker.blitzi.me](https://ticker.blitzi.me) – kein Rate-Limit, kein API-Key
- Automatisches Refresh alle 5 Minuten

---

## Hardware

| Teil | Details |
|------|---------|
| Board | Elecrow CrowPanel 7.0" ESP32-S3 |
| Display | 800 × 480 RGB, Kapazitiv-Touch |
| I/O | PCA9557 GPIO-Expander (I2C) |
| Anschluss | USB-C (Flashing + Betrieb) |

---

## Web Installer

Kein Build nötig – einfach im Browser flashen:

**[flashmanbTC.github.io/flashticker_7zoll](https://flashmanbTC.github.io/flashticker_7zoll/)**

Erfordert **Chrome** oder **Edge** (Web Serial API).

### Einrichtung

1. CrowPanel per USB-C verbinden
2. Auf **FLASHTICKER installieren** klicken, Gerät auswählen
3. Warten bis Firmware geflasht ist und Board neu startet (~30 Sek.)
4. Mit WLAN-Hotspot `FLASHTICKER` (PW: `flashticker`) verbinden
5. Im Browser das Heimnetz auswählen und Passwort eingeben
6. Fertig – Kurse werden geladen

---

## Build (PlatformIO)

```bash
cd esp32s3
pio run
pio run --target upload
```

Abhängigkeiten werden automatisch über `platformio.ini` geladen.

---

## Changelog

### v3.2 (2026-04-15)
- 1H Chart-Periode hinzugefügt
- Period-Buttons [1H][1T][1W][1M] in Status Bar (neben Währungsbuttons)
- Trenner zwischen Perioden- und Währungsbuttons
- Hi/Lo im Vollbild-Modus unten positioniert (mehr Chartfläche)
- Dark Badge hinter Hi/Lo Labels (immer lesbar über Chart-Linie)

### v3.1 (2026-04-14)
- Loading Screen: Fortschrittsbalken füllt sich korrekt (lv_bar, orange)
- Kein Flash mehr zwischen Bootscreen und Loading Screen
- Prozentanzeige größer (20pt) und unterhalb des Balkens
- 5-Min-Hintergrundrefresh ohne Loading Screen
- Bug Fix: Bottom-Cards korrekt bei Silber/Gold als Boot-Default

### v3.0 (2026-04-14)
- Einstellungen-Screen (Zahnrad im Header)
- Boot-Defaults konfigurierbar: Asset, Währung, Chart-Periode, Vollbild
- Einstellungen werden in NVS gespeichert (überleben Neustart)
- Version auf Bootscreen und im Einstellungen-Header
- Loading Screen mit Fortschrittsbalken beim Start

### v2.0 (2026-04-14)
- Eigene API: [ticker.blitzi.me](https://ticker.blitzi.me) (kein CoinGecko/Binance mehr)
- Kein Rate-Limit-Delay mehr beim API-Abruf
- Alle Chart-Perioden beim Boot vorgeladen → kein UI-Lag beim Wechsel
- Vollbild-Modus für Top-Card: 72pt Preis + 24h High/Low
- Custom Fonts: Montserrat 64pt und 72pt (LVGL-kompatibel)
- QR-Code im WiFi-Setup-Screen (WIFI:S:... Standard)
- WiFiManager ohne Timeout (kein WLAN = keine Funktion)
- Screen-Clear nach WiFi-Setup (verhindert Bildschirm-Artefakte)
- Umlaut-Bug im WiFi-Screen behoben
- Label-Positionierung vereinheitlicht (TOP_LEFT)

### v1.0 (2026-04-04)
- Erstveröffentlichung
- BTC / Gold / Silber in USD · EUR · CHF
- LovyanGFX + LVGL 8.3 auf CrowPanel 7.0"
- WiFiManager Portal · NTP Uhrzeit · Touch UI
- Linien-Chart mit 1T / 1W / 1M Perioden

---

<p align="center">MIT License · Made by <a href="https://github.com/FlashmanBTC">FlashmanBTC</a></p>

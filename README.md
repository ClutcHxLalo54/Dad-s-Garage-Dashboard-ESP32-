# Dad's Garage Dashboard (ESP32)

A custom touchscreen dashboard built on an ESP32 with a 7” display.

This project combines:

* 🕒 Real-time clock (NTP synced)
* 🌦️ Live weather (Open-Meteo API)
* 🎣 Fishing conditions (Navarre, FL optimized)
* 🎨 Drawing app
* 🎛️ Custom UI with themes
* 🐟 Hidden easter egg

---

## 🔧 Features

### Clock + Weather

* Syncs time using WiFi (NTP)
* Displays temperature + conditions
* Updates automatically every 10 minutes

### Fishing Advisor (Navarre, FL)

* Uses real sunrise/sunset times
* Evaluates:

  * Time of day (sunrise/sunset windows)
  * Wind + gusts
  * Weather conditions
* Outputs:

  * **GO NOW / OKAY / WAIT**
  * Best fishing window
  * Reason + advice

### Drawing App

* Touch-based drawing
* Adjustable brush size + color
* Clear canvas

### Settings

* Multiple themes
* 12/24 hour toggle
* Seconds toggle

### Easter Egg

* Hidden fish icon in header 🐟
* Tap it to reveal a secret screen

---

## 🌍 API Used

* [Open-Meteo](https://open-meteo.com/) (free, no API key required)

---

## 📡 WiFi Setup

Update these lines in the code:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_PASSWORD";
```

---

## 📍 Location Config

Currently set to:

* Navarre, Florida

```cpp
const float WEATHER_LAT = 30.4016;
const float WEATHER_LON = -86.8636;
```

---

## ⚙️ Hardware

* ESP32 (S3 recommended)
* 7” RGB touchscreen (GT911 touch)
* PCA9557 IO expander

---

## 💡 Notes

* Weather updates every 10 minutes
* Fishing updates every minute
* Falls back to internal clock if WiFi fails

---

## 👨‍💻 Author

Caleb

Built as a custom project for my dad's garage.

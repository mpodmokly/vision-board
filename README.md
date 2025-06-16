# Vision Board
## How to run
Install Espressif Framework (v5.4.1): [ESP-IDF](https://dl.espressif.com/dl/esp-idf/)

Build and flash project (ESP-IDF CMD)
```
idf.py build
idf.py -p COMx flash monitor
```
where x is the nuber of COM port.
## Dataset for model
German Road Signs Images: [Download](https://sid.erda.dk/public/archives/daaeac0d7ce1152aea9b61d9f1e19370/published-archive.html)

Used classes:
* 50 speed limit (2)
* give way (13)
* STOP (14)
* no vehicles (15)
* no entry (17)
* pedestrian crossin (27)

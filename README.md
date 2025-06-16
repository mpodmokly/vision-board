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
* 50 speed limit: [2](model/images/train/00002/)
* give way: [13](model/images/train/00013/)
* STOP: [14](model/images/train/00014/)
* no vehicles: [15](model/images/train/00015/)
* no entry: [17](model/images/train/00017/)
* pedestrian crossin: [27](model/images/train/00027/)

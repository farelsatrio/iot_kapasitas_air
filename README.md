# TankPilot

## Instalasi

1. clone repository ke local:
```
git clone https://github.com/farelsatrio/iot_kapasitas_air.git
```

2. masuk ke direktori proyek:
```
cd iot_kapasitas_air 
```

3. build image:
```
docker build -t python_app .
```

4. jalankan image:
```
docker run -d -p 8000:8000 python_app
```

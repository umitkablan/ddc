## Device Data Congestor - DDC

### Development Environment
Make sure docker is installed to set up compile environment via docker (build image and execute into development environment):
```sh
docker build -f docker/Dockerfile . -t ddc
```
After building the image, execute into it while sharing source directory:
```sh
docker run -ti -v /path/to/host/source/ddc:/SRC/ddc ddc bash
```

### Run Test
From now on you can build DDC source and run it:

```sh
cd /SRC/ddc

cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=bin
cmake --build build/Debug
cmake --install build/Debug

cd scripts
./services.sh start
./services.sh check  # no output is expected
./services.sh setup

./services.sh ddc_start

<< send fake data & check counter endpoint via curl >>

./services.sh ddc_stop
./services.sh stop
```

Sample testing with fake data:
```sh
# Send 1000 fake "ps" measurements from device "dev-001-ps"
(sleep .5; for i in $(seq 1 1000); do echo $i; sleep .2; done) | ../bin/bin/filefakedev "dev-001-ps" "ps" - fake_dev_adapter.conf &

# check result from counter service
curl http://localhost:18080/measurements
```


# Configure

Make sure you use a `vtk-wasm-sdk` image that supports `threads`.

## Linux

```sh
docker run \
    --rm \
    -v $(pwd):/work \
    kitware/vtk-wasm-sdk:wasm32-threads-v9.3.1-3748-g9ec2f0cc8c-20240806 \
    emcmake cmake -S /work -B /work/build -DCMAKE_BUILD_TYPE=Release -DVTK_DIR=/VTK-install/Release/lib/cmake/vtk
```

## Windows

```sh
docker run `
    --rm `
    -v ${pwd}:/work `
    kitware/vtk-wasm-sdk:wasm32-threads-v9.3.1-3748-g9ec2f0cc8c-20240806 `
    emcmake cmake -S /work -B /work/build -DCMAKE_BUILD_TYPE=Release -DVTK_DIR=/VTK-install/Release/lib/cmake/vtk
```
# Build

## Linux

```sh
docker run \
    --rm \
    -v $(pwd):/work \
    kitware/vtk-wasm-sdk:wasm32-threads-v9.3.1-3748-g9ec2f0cc8c-20240806 \
    cmake --build /work/build
```

## Windows

```sh
docker run `
    --rm `
    -v ${pwd}:/work `
    kitware/vtk-wasm-sdk:wasm32-threads-v9.3.1-3748-g9ec2f0cc8c-20240806 `
    cmake --build /work/build
```

# Run

`server.py` configures COEP, COOP headers for `SharedArrayBuffer`

```sh
python3 ./server.py
```

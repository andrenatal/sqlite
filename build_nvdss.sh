# setup emscripten

git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
source emsdk_env.sh
which emcc
cd ..
./configure --enable-all
cd ext/wasm/
make
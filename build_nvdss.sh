# setup emscripten

sudo apt install wabt
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source emsdk_env.sh
which emcc
cd ..
./configure --enable-all
make sqlite3.c
cat sqlite-ndvss/sqlite-ndvss.c >> sqlite3.c
cd ext/wasm/
make
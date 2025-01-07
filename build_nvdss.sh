# setup emscripten

sudo apt install wabt
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
source emsdk_env.sh
which emcc
cd ..
./configure --enable-all
make sqlite3.c
cat sqlite-ndvss/sqlite-ndvss.c >> sqlite3.c
cd ext/wasm/
make

wget https://sqlite.org/althttpd/tarball/07d1ade99f/althttpd-07d1ade99f.tar.gz
gunzip -c althttpd-07d1ade99f.tar.gz | tar xvf -
cd althttpd-07d1ade99f
make althttpd
cd ../
cd ext/wasm/
../../althttpd-07d1ade99f/althttpd --enable-sab --max-age 1
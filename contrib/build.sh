#!/bin/bash
set -e
VERSION="10.5-beta2"

# Linux static build
if [ ! -d dependslinux ]; then
  mkdir dependslinux
fi
cd dependslinux
if [ ! -f gmp-6.1.2.tar.lz ]; then
  wget https://ftp.gnu.org/gnu/gmp/gmp-6.1.2.tar.lz
fi
if [ ! -f libsodium-1.0.17.tar.gz ]; then
  wget https://download.libsodium.org/libsodium/releases/libsodium-1.0.17.tar.gz
fi
if [ ! -f zeromq-4.3.1.tar.gz ]; then
  wget https://github.com/zeromq/libzmq/releases/download/v4.3.1/zeromq-4.3.1.tar.gz
fi
if [ ! -f protobuf-cpp-3.6.1.tar.gz ]; then
  wget https://github.com/protocolbuffers/protobuf/releases/download/v3.6.1/protobuf-cpp-3.6.1.tar.gz
fi
if [ ! -f cuda_11.2.0_460.27.04_linux.run ]; then
  wget https://developer.download.nvidia.com/compute/cuda/11.2.0/local_installers/cuda_11.2.0_460.27.04_linux.run
fi
if [ ! -d CLRX-mirror ]; then
  git clone https://github.com/CLRX/CLRX-mirror.git
fi
if [ ! -d dist ]; then
  mkdir dist
fi
cd dist
export DEPENDS=`pwd`
if [ ! -d tmp ]; then
  mkdir tmp
fi
if [ ! -d toolkit ]; then
  mkdir toolkit
fi
if [ ! -d samples ]; then
  mkdir samples
fi
cd ..

# gmp
tar --lzip -xvf gmp-6.1.2.tar.lz
cd gmp-6.1.2
./configure --prefix=$DEPENDS --enable-cxx --enable-static --disable-shared 
make 
make install
cd ..

# sodium
tar -xzf libsodium-1.0.17.tar.gz
cd libsodium-1.0.17
./configure --prefix=$DEPENDS --enable-static --disable-shared 
make 
make install
cd ..

# zmq
tar -xzf zeromq-4.3.1.tar.gz
cd zeromq-4.3.1
# disable glibc 2.25 'getrandom' usage for compatibility
sed -i 's/libzmq_cv_getrandom=\"yes\"/libzmq_cv_getrandom=\"no\"/' configure
./configure --prefix=$DEPENDS --enable-static --disable-shared 
make 
make install
cd ..

# protobuf
tar -xzf protobuf-cpp-3.6.1.tar.gz
cd protobuf-3.6.1
./configure --prefix=$DEPENDS --enable-static --disable-shared
make 
make install
cd ..

# CLRX
cd CLRX-mirror
if [ ! -d build ]; then
  mkdir build
fi
cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEPENDS
make
make install
rm $DEPENDS/lib64/libCLRX*.so*
cd ../../
#cuda
sudo sh cuda_11.2.0_460.27.04_linux.run --toolkit --toolkitpath=$DEPENDS/toolkit --samples --samplespath=$DEPENDS/samples --tmpdir=$DEPENDS/tmp --silent

export PATH=$DEPENDS/bin:$DEPENDS/toolkit/bin:$PATH
export LD_LIBRARY_PATH=$DEPENDS/toolkit/lib64:$DEPENDS/toolkit/lib64/stubs:$DEPENDS/toolkit/lib64/nvrtc-prev:$LD_LIBRARY_PATH
export C_INCLUDE_PATH=$C_INCLUDE_PATH:$DEPENDS/include
export CPLUS_INCLUDE_PATH=$CPLUS_INCLUDE_PATH:$DEPENDS/include
cd ..

if [ ! -d buildlinux ]; then
  mkdir buildlinux
fi
cd  buildlinux
cmake ../src -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEPENDS -DSTATIC_BUILD=ON -DOpenCL_INCLUDE_DIR=$DEPENDS/toolkit/include -DOpenCL_LIBRARY=$DEPENDS/toolkit/lib64/libOpenCL.so
possibly another choice is
(cmake ../src -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEPENDS -DSTATIC_BUILD=ON -DOpenCL_INCLUDE_DIR=$DEPENDS/toolkit/include -DOpenCL_LIBRARY=$DEPENDS/toolkit/lib64/libOpenCL.so  -DCUDA_driver_LIBRARY=$DEPENDS/toolkit/lib64/stubs/libcuda.so -DCUDA_CUDA_LIBRARY=$DEPENDS/toolkit/lib64/libcudart.so -DCUDA_nvrtc_LIBRARY=$DEPENDS/toolkit/lib64/libnvrtc.so)
make
strip xpmclient
strip xpmclientnv

# make NVidia distr
mkdir xpmclient-cuda-$VERSION-linux
cd xpmclient-cuda-$VERSION-linux
cp ../xpmclientnv ./miner
echo "#/bin/bash" > xpmclientnv
echo "DIR=\$(dirname \"\$0\")" >> xpmclientnv
echo "LD_LIBRARY_PATH=\$DIR/. ./miner \$@" >> xpmclientnv
chmod +x xpmclientnv
cp ../../src/xpm/cuda/config.txt .
mkdir -p xpm/cuda
cp ../../src/xpm/cuda/*.cu xpm/cuda
cp $DEPENDS/toolkit/lib64/nvrtc-prev/libnvrtc.so.11.2 .
cp $DEPENDS/toolkit/lib64/nvrtc-prev/libnvrtc-builtins.so.11.2 .
cd ..
tar -czf xpmclient-cuda-$VERSION-linux.tar.gz xpmclient-cuda-$VERSION-linux


# make AMD(OpenCL) distr
mkdir xpmclient-opencl-$VERSION-linux
cd xpmclient-opencl-$VERSION-linux
cp ../xpmclient .
cp ../../src/xpm/opencl/config.txt .
mkdir -p xpm/opencl
cp ../../src/xpm/opencl/generic_* xpm/opencl
cp ../../src/xpm/opencl/gcn_* xpm/opencl
cd ..
tar -czf xpmclient-opencl-$VERSION-linux.tar.gz xpmclient-opencl-$VERSION-linux
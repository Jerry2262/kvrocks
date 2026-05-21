rm -rf build/
source /opt/openEuler/gcc-toolset-14/enable
./x.py build -j`nproc` \
-D FETCHCONTENT_SOURCE_DIR_ROCKSDB=../../rocksdb \
-D CMAKE_CXX_FLAGS="-march=armv8.3-a+rcpc -w" \
-D CMAKE_C_FLAGS="-march=armv8.3-a+rcpc -w"
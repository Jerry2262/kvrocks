rm -rf build/

march_flags=""
if [ -f /opt/openEuler/gcc-toolset-14/enable ] && [ "$(uname -m)" = "aarch64" ]; then
    source /opt/openEuler/gcc-toolset-14/enable
    march_flags="-march=armv8.3-a+rcpc"
fi

./x.py build -j`nproc` --unittest \
-D FETCHCONTENT_SOURCE_DIR_ROCKSDB=../../rocksdb \
-D CMAKE_CXX_FLAGS="$march_flags -w" \
-D CMAKE_C_FLAGS="$march_flags -w"
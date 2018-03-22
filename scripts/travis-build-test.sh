#!/bin/bash -e
cd src
case "${TRAVIS_TARGET-unspecified}" in
uspace|unspecified)
    ./autogen.sh
    ./configure --with-realtime=uspace --disable-check-runtime-deps
    make -j2  # cores: ~2, bursted
    ../scripts/rip-environment runtests
;;
docs)
    ./autogen.sh
    ./configure --with-realtime=uspace --disable-check-runtime-deps --enable-build-documentation=yes
    make -j2  docs # cores: ~2, bursted
;;
rtai)
    ./autogen.sh
    ./configure --with-realtime=/usr/realtime-2.6.32-122-rtai --disable-check-runtime-deps
    make -j2  docs # cores: ~2, bursted
esac

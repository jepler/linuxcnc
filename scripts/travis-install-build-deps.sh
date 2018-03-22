#!/bin/bash -e
case "${TRAVIS_TARGET-unspecified}" in
uspace|unspecified|docs) ;;
rtai)
    wget \
        http://linuxcnc.org/dists/jessie/base/binary-amd64/linux-headers-3.16.0-9-rtai-amd64_3.16.7-6linuxcnc_amd64.deb \
        http://linuxcnc.org/dists/jessie/base/binary-amd64/linux-image-3.16.0-9-rtai-amd64_3.16.7-6linuxcnc_amd64.deb \
        http://linuxcnc.org/dists/jessie/base/binary-amd64/rtai-modules-3.16.0-9_5.0~test1.2015.12.06.23.g613a3e2_amd64.deb
    sudo dpkg -i *.deb ;;
esac
sudo apt-get update -qq
sudo apt-get install -y devscripts equivs build-essential --no-install-recommends
sudo apt-get remove -f libreadline6-dev || true
sudo apt-get remove -f libreadline-dev || true
debian/configure uspace
mk-build-deps -i -r -s sudo -t 'apt-get --no-install-recommends --no-install-suggests'

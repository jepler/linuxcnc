#!/bin/sh -e
set -x
cat /etc/os-release
. /etc/os-release
CODENAME=${UBUNTU_CODENAME-`echo "$VERSION" | grep -o '[a-z]*'`}
echo "Distro is $ID-${CODENAME}"
case "$ID-${CODENAME}" in
    debian-wheezy)
	sudo sh -c 'cat > /etc/apt/sources.list' <<- EOF
	deb http://www.linuxcnc.org/ $CODENAME base
	deb-src http://www.linuxcnc.org/ $CODENAME base
	deb http://archive.debian.org/debian/ $CODENAME main contrib
	deb-src http://archive.debian.org/debian/ $CODENAME main contrib
	EOF
    ;;
    debian-*)
	sudo sh -c 'cat >> /etc/apt/sources.list' <<- EOF
	deb http://www.linuxcnc.org/ $CODENAME base
	deb-src http://www.linuxcnc.org/ $CODENAME base
	deb-src http://deb.debian.org/debian $CODENAME main contrib
	EOF
    ;;
    ubuntu-*)
	sudo sh -c "echo deb-src http://us.archive.ubuntu.com/ubuntu/ $CODENAME main universe >> /etc/apt/sources.list"
    ;;
esac

grep . /etc/apt/sources.list /etc/apt/sources.list.d/* || true
sudo apt-get update -qq
sudo apt-get install -y lsb-release python devscripts equivs build-essential --no-install-recommends
sudo apt-get remove -f libreadline6-dev || true
sudo apt-get remove -f libreadline-dev || true
case "${REALTIME-uspace}" in
uspace)
    debian/configure uspace noauto
;;
rtai)
    debian/configure rtai
;;
esac
grep . debian/control /dev/null
mk-build-deps
sudo dpkg -i linuxcnc-*.deb || true
sudo apt-get -f install -y --no-install-recommends


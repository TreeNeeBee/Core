DESCRIPTION = "Core Manager" 
SECTION = "middleware" 
LICENSE = "CLOSED" 
PR = "r0"
PV = "0.1+git${SRCPV}"

SRC_URI = "http://XXX.com/frameworks;protocol=http;tag=v0.1"
SRCREV = "${AUTOREV}

S = "${WORKDIR}/git"

inherit pkgconfig cmake
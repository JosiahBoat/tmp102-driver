SUMMARY = "TMP102 temperature sensor driver (user-space)"
DESCRIPTION = "Reads temperature from TMP102 I2C sensor and outputs via FIFO"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://temp_reader.c \
           file://start_temp_reader"

S = "${WORKDIR}"

# Compile C code
do_compile() {
    ${CC} ${CFLAGS} temp_reader.c -o temp_reader
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 temp_reader ${D}${bindir}

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 start_temp_reader ${D}${sysconfdir}/init.d
}

FILES:${PN} += "${sysconfdir}/init.d/start_temp_reader"

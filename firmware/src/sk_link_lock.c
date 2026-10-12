/* SPDX-License-Identifier: GPL-3.0-only
 * Cable-3 SKL lock SysEx: F0 7D 53 4B 4C … F7
 * Answers HELLO with build version + CRC. Full snapshot transfer stays host-side
 * until flash path is wired; see host/README.md. */
#if FELUCCA_OTA

enum {
    SKL_H0 = 0x7D,
    SKL_H1 = 0x53,
    SKL_H2 = 0x4B,
    SKL_H3 = 0x4C,
    SKL_HELLO = 0x01,
    SKL_HELLO_ACK = 0x02,
    SKL_CRC = 0x03,
    SKL_CRC_ACK = 0x04
};

static uint32_t skl_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i, b;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (b = 0; b < 8u; b++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}

static uint32_t skl_build_crc(void)
{
    const char *v = FELUCCA_VERSION;
    uint32_t n = 0;
    while (v[n])
        n++;
    return skl_crc32((const uint8_t *)v, n);
}

static void skl_send(uint8_t cmd, const uint8_t *pay, uint32_t np)
{
    uint8_t f[64];
    uint32_t n = 0, i;
    f[n++] = 0xF0;
    f[n++] = SKL_H0;
    f[n++] = SKL_H1;
    f[n++] = SKL_H2;
    f[n++] = SKL_H3;
    f[n++] = cmd;
    for (i = 0; i < np && n + 1u < sizeof f; i++)
        f[n++] = (uint8_t)(pay[i] & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}

static void skl_send_crc_ack(void)
{
    uint32_t crc = skl_build_crc();
    uint8_t p[5] = {
        (uint8_t)(crc & 0x7Fu),
        (uint8_t)((crc >> 7) & 0x7Fu),
        (uint8_t)((crc >> 14) & 0x7Fu),
        (uint8_t)((crc >> 21) & 0x7Fu),
        (uint8_t)((crc >> 28) & 0x0Fu)
    };
    skl_send(SKL_CRC_ACK, p, 5);
}

/* main loop: SKL before editor so unknown FL frames are not held. */
static int sk_link_service(void)
{
    const uint8_t *p;
    uint32_t n, k;
    uint8_t pay[32];
    const char *hv;
    if (!ota_frame_get(&p, &n) || n < 4u)
        return 0;
    if (p[0] != SKL_H0 || p[1] != SKL_H1 || p[2] != SKL_H2 || p[3] != SKL_H3)
        return 0;
    /* payload is bytes between F0 and F7: HDR0..HDR3 cmd … */
    if (n >= 5u && p[4] == SKL_HELLO) {
        hv = FELUCCA_VERSION;
        for (k = 0; hv[k] && k + 1u < sizeof pay; k++)
            pay[k] = (uint8_t)hv[k];
        pay[k++] = 0;
        ota_frame_done();
        skl_send(SKL_HELLO_ACK, pay, k);
        skl_send_crc_ack();
        return 1;
    }
    if (n >= 5u && p[4] == SKL_CRC) {
        ota_frame_done();
        skl_send_crc_ack();
        return 1;
    }
    ota_frame_done(); /* consume unknown SKL cmds so the OTA slot frees */
    return 1;
}

#endif /* FELUCCA_OTA */

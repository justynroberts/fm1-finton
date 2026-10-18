/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton song slots: six of 16 KiB at SONG_AREA (platform.c), each the song_t then its CRC32.
 * A save erases three sectors with the interrupts off, so the sound stops for ~150 ms: the UI
 * saves only while stopped. Main loop only. */

#define NSLOT 6u
#define SLOT_SIZE 0x4000u
typedef char song_fits_a_slot[(sizeof(song_t) + 4u <= SLOT_SIZE) ? 1 : -1];

#ifdef DINGHY_HOST
static uint8_t host_flash[NSLOT * SLOT_SIZE];
static int slot_read(uint32_t off, void *p, uint32_t n) { memcpy(p, host_flash + off, n); return 0; }
static int slot_erase(uint32_t off) { memset(host_flash + off, 0xFF, 0x1000u); return 0; }
static int slot_write(uint32_t off, const void *p, uint32_t n) { memcpy(host_flash + off, p, n); return 0; }
#else
static int slot_read(uint32_t off, void *p, uint32_t n) { return flash_ok ? st_read(SONG_AREA + off, p, n) : -1; }
static int slot_erase(uint32_t off) { uint32_t took; return flash_ok ? fl_erase4k(SONG_AREA + off, &took) : -1; }
static int slot_write(uint32_t off, const void *p, uint32_t n) { return flash_ok ? fl_write(SONG_AREA + off, p, n) : -1; }
#endif

static song_t song_io;                          /* the copy loads land in, so a bad slot never touches the song */

static int song_save(uint32_t slot)
{
    uint32_t base = (slot % NSLOT) * SLOT_SIZE, k, crc;
    int rc;
    crc = crc32((const uint8_t *)&song, sizeof song);
    for (k = 0; k < SLOT_SIZE; k += 0x1000u)
        if ((rc = slot_erase(base + k)) != 0) return rc;
    if ((rc = slot_write(base + 4u, (const uint8_t *)&song + 4u, sizeof song - 4u)) != 0) return rc;
    if ((rc = slot_write(base + sizeof song, (const uint8_t *)&crc, 4u)) != 0) return rc;
    return slot_write(base, (const uint8_t *)&song, 4u);      /* the magic last: a cut save stays invalid */
}

static int song_load(uint32_t slot)
{
    uint32_t base = (slot % NSLOT) * SLOT_SIZE, crc;
    if (slot_read(base, &song_io, sizeof song_io) || slot_read(base + sizeof song_io, &crc, 4u))
        return -1;
    if (song_io.magic != SONG_MAGIC || song_io.ver != SONG_VER || crc32((const uint8_t *)&song_io, sizeof song_io) != crc)
        return -2;
    memcpy(&song, &song_io, sizeof song);
    return 0;
}

static int slot_used(uint32_t slot)
{
    uint32_t m = 0;
    slot_read((slot % NSLOT) * SLOT_SIZE, &m, 4u);
    return m == SONG_MAGIC;
}

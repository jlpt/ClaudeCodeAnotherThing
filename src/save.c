/*
 * Game progress saved to the cartridge's 4 Kbit EEPROM (like most first
 * party N64 games). The record is tiny: story step, learned spells, max HP.
 */
#include "game.h"

#define SAVE_MAGIC   0x4D543634u   /* "MT64" */
#define SAVE_VERSION 1

save_t g_save;
static bool eeprom_ok;
static save_t stored;
static bool stored_valid;

static uint8_t checksum(const save_t *s)
{
    const uint8_t *b = (const uint8_t *)s;
    uint8_t sum = 0xA5;
    for (size_t i = 0; i < offsetof(save_t, checksum); i++) sum = (uint8_t)((sum << 1 | sum >> 7) ^ b[i]);
    return sum;
}

void save_reset(void)
{
    memset(&g_save, 0, sizeof(g_save));
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.max_hp = 6;
    g_save.cleared = stored_valid ? stored.cleared : 0;
}

void save_init(void)
{
    eeprom_ok = eeprom_present() != EEPROM_NONE;
    stored_valid = false;
    if (eeprom_ok) {
        eeprom_read_bytes(&stored, 0, sizeof(stored));
        stored_valid = stored.magic == SAVE_MAGIC && stored.version == SAVE_VERSION &&
                       stored.checksum == checksum(&stored) && stored.step < ST_COUNT;
    }
    save_reset();
}

bool save_available(void) { return eeprom_ok; }

bool save_exists(void) { return stored_valid && stored.step > 0; }

bool save_load(void)
{
    if (!stored_valid) return false;
    g_save = stored;
    return true;
}

bool save_write(void)
{
    g_save.magic = SAVE_MAGIC;
    g_save.version = SAVE_VERSION;
    g_save.checksum = checksum(&g_save);
    stored = g_save;
    stored_valid = true;
    if (!eeprom_ok) return false;
    eeprom_write_bytes(&g_save, 0, sizeof(g_save));
    return true;
}

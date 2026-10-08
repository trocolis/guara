#include <string.h>
#include <unity.h>

#include "ini.h"
#include "midi.h"

void setUp(void) {}
void tearDown(void) {}

/* Tipo 0, 480 ticks/semínima, 120 BPM: C4 por 1 semínima, depois E4 e G4 juntos (toca G4). */
static const uint8_t MIDI_TEST[] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
    'M', 'T', 'r', 'k', 0, 0, 0, 30,
    0x00, 0x90, 60, 100,     /* C4 on */
    0x83, 0x60, 0x80, 60, 0, /* +480: C4 off */
    0x00, 0x90, 64, 100,     /* E4 on */
    0x00, 0x90, 67, 100,     /* G4 on */
    0x83, 0x60, 0x80, 64, 0, /* +480: E4 off */
    0x00, 0x80, 67, 0,       /* G4 off */
    0x00, 0xFF, 0x2F, 0x00,  /* fim */
};

static void test_midi_monophonic(void)
{
    midi_t m;
    midi_note_t n;
    TEST_ASSERT_TRUE(midi_open(&m, MIDI_TEST, sizeof(MIDI_TEST)));
    TEST_ASSERT_TRUE(midi_next(&m, &n));
    TEST_ASSERT_EQUAL_UINT16(261, n.freq);
    TEST_ASSERT_EQUAL_UINT16(500, n.ms);
    TEST_ASSERT_TRUE(midi_next(&m, &n));
    TEST_ASSERT_EQUAL_UINT16(391, n.freq); /* G4: a mais aguda */
    TEST_ASSERT_EQUAL_UINT16(500, n.ms);
    TEST_ASSERT_FALSE(midi_next(&m, &n));
    TEST_ASSERT_EQUAL_UINT32(1000, midi_duration_ms(MIDI_TEST, sizeof(MIDI_TEST)));
}

static void test_midi_invalid(void)
{
    midi_t m;
    static const uint8_t garbage[] = "nao e midi nenhum";
    TEST_ASSERT_FALSE(midi_open(&m, garbage, sizeof(garbage)));
}

static void test_frequencies(void)
{
    TEST_ASSERT_EQUAL_UINT16(440, midi_frequency(69));
    TEST_ASSERT_EQUAL_UINT16(880, midi_frequency(81));
    TEST_ASSERT_EQUAL_UINT16(220, midi_frequency(57));
}

static void test_ini(void)
{
    const char *ini = "[GAME]\ntitle = Boitata\n; comentario\nicon=icon.bmp\n\n[GUARA_EFFECTS]\ncost_energy_per_min=3\r\n";
    char buf[32];
    TEST_ASSERT_TRUE(ini_text(ini, "GAME", "title", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Boitata", buf);
    TEST_ASSERT_FALSE(ini_text(ini, "GAME", "cost_energy_per_min", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(3, ini_integer(ini, "guara_effects", "cost_energy_per_min", 0));
    TEST_ASSERT_EQUAL_INT(7, ini_integer(ini, "GUARA_EFFECTS", "inexistente", 7));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_midi_monophonic);
    RUN_TEST(test_midi_invalid);
    RUN_TEST(test_frequencies);
    RUN_TEST(test_ini);
    return UNITY_END();
}

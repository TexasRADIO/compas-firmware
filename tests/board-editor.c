#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../board/board.h"

static uint8_t input[160], output[160];
static uint32_t input_length, output_length;
static unsigned input_ready, consumed, submitted;
static BoardCommand last_command;
static BoardView view;
static uint8_t effects[FX_PARAMS];

static int ota_wire_send(const uint8_t *bytes, uint32_t length)
{
    assert(length <= sizeof output);
    memcpy(output, bytes, length); output_length = length; return 0;
}
static int ota_frame_get(const uint8_t **bytes, uint32_t *length)
{
    if (!input_ready) return 0;
    *bytes = input; *length = input_length; return 1;
}
static void ota_frame_done(void) { input_ready = 0; consumed++; }

BoardView board_view(unsigned role) { (void)role; return view; }
void board_fx_view(uint8_t out[FX_PARAMS]) { memcpy(out, effects, FX_PARAMS); }
unsigned board_submit(BoardCommand command)
{ last_command = command; submitted++; return CP_OK; }
uint32_t cp_status(uint32_t key) { return key == CP_PHRASE ? 7u : key == CP_MUTES ? view.muted : 0u; }

#include "../board/editor.inc"

static void request(uint8_t type, uint16_t seq, const uint8_t *payload, unsigned length)
{
    const uint8_t prefix[] = {BB_ED_MFR_0, BB_ED_MFR_1, BB_ED_MFR_2, BB_ED_DEVICE, BB_ED_FAMILY};
    memcpy(input, prefix, sizeof prefix);
    input[5] = type; input[6] = (uint8_t)(seq >> 7); input[7] = (uint8_t)(seq & 127u);
    if (length) memcpy(input + 8, payload, length);
    input_length = 8u + length; input_ready = 1; output_length = 0;
    compas_editor_service();
}
static uint8_t reply_type(void)
{
    assert(output_length >= 10u && output[0] == 0xF0u && output[output_length - 1] == 0xF7u);
    return output[6];
}

int main(void)
{
    view.bpm = 120; view.feel = 54; view.study = 3; view.section = 1; view.pending = 3;
    view.position = 9; view.levels[0] = 81; view.levels[5] = 76;
    effects[FX_TIEMPO] = 2; effects[FX_REPITE] = 35;
    compas_editor_init();

    const uint8_t hello[] = {BB_ED_PROTOCOL_VERSION};
    request(BB_ED_MSG_HELLO, 1, hello, sizeof hello);
    assert(reply_type() == BB_ED_MSG_HELLO_REPLY && output[10] == BB_ED_PRODUCT_COMPAS);

    request(BB_ED_MSG_SNAPSHOT_BEGIN, 2, 0, 0);
    /* The second immediate send is SNAPSHOT_END; both messages were formed and
     * the last one reached the test wire without consuming updater traffic. */
    assert(reply_type() == BB_ED_MSG_SNAPSHOT_END);

    const uint8_t begin[] = {0, 0, 0, 1, 7, 0, 9};
    request(BB_ED_MSG_TX_BEGIN, 3, begin, sizeof begin);
    assert(reply_type() == BB_ED_MSG_ACK);
    const uint8_t set_bpm[] = {7, 0, 0x20, 0x01, 9, 48}; /* field 0x1001, value 1200 */
    request(BB_ED_MSG_SET, 4, set_bpm, sizeof set_bpm);
    assert(reply_type() == BB_ED_MSG_APPLIED);
    const uint8_t commit[] = {0, 9};
    request(BB_ED_MSG_TX_COMMIT, 5, commit, sizeof commit);
    assert(reply_type() == BB_ED_MSG_ACK && submitted == 1u);
    assert(last_command.command == CP_BPM && last_command.a == 120u);

    request(BB_ED_MSG_PLAY, 6, 0, 0);
    assert(reply_type() == BB_ED_MSG_ACK && last_command.command == CP_START);

    /* A non-editor frame remains available to ota_service(). */
    input[4] = 0x7Fu; input_length = 6; input_ready = 1;
    unsigned before = consumed; compas_editor_service();
    assert(input_ready && consumed == before);
    return 0;
}

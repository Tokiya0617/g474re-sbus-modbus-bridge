/* Host-only test: never add this main() to the STM32 firmware target. */
#include "sbus_receiver.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "These tests require assertions; do not define NDEBUG."
#endif

/* Fixed bytes, independent of any frame encoder: CH1=1, CH2=2, CH16=1024. */
static const uint8_t frame[SBUS_FRAME_SIZE] = {
    [0] = 0x0F, [1] = 0x01, [2] = 0x10, [22] = 0x80
};

static void check_equal(const SbusFrame *actual, const SbusFrame *expected)
{
    for (size_t ch = 0U; ch < SBUS_CHANNEL_COUNT; ++ch)
    {
        assert(actual->channels[ch] == expected->channels[ch]);
    }
    assert(actual->frame_lost == expected->frame_lost);
    assert(actual->failsafe == expected->failsafe);
}

/* Also verify that every false return preserves the entire output object. */
static bool push(SbusReceiver *receiver, uint8_t byte, SbusFrame *out)
{
    unsigned char before[sizeof(*out)];
    memcpy(before, out, sizeof(*out));
    const bool ready = sbus_receiver_push_byte(receiver, byte, out);
    assert(receiver->length < SBUS_FRAME_SIZE);
    if (!ready)
    {
        assert(memcmp(before, out, sizeof(*out)) == 0);
    }
    return ready;
}

static void check_frame(SbusReceiver *receiver, const uint8_t *bytes)
{
    SbusFrame actual = {0};
    SbusFrame expected;
    assert(sbus_decode_frame(bytes, SBUS_FRAME_SIZE, &expected));
    for (size_t i = 0U; i < SBUS_FRAME_SIZE; ++i)
    {
        assert(push(receiver, bytes[i], &actual) == (i == SBUS_FRAME_SIZE - 1U));
    }
    check_equal(&actual, &expected);
    assert(receiver->length == 0U);
}

static void test_chunks_and_noise(void)
{
    SbusReceiver receiver;
    SbusFrame out = {0};
    SbusFrame expected;
    assert(sbus_decode_frame(frame, sizeof(frame), &expected));

    /* Every possible two-chunk split, including empty first/last chunk. */
    for (size_t split = 0U; split <= SBUS_FRAME_SIZE; ++split)
    {
        sbus_receiver_init(&receiver);
        for (size_t i = 0U; i < split; ++i)
            assert(push(&receiver, frame[i], &out) == (i == SBUS_FRAME_SIZE - 1U));
        assert(receiver.length == (split % SBUS_FRAME_SIZE));
        for (size_t i = split; i < SBUS_FRAME_SIZE; ++i)
            assert(push(&receiver, frame[i], &out) == (i == SBUS_FRAME_SIZE - 1U));
        check_equal(&out, &expected);
    }

    sbus_receiver_init(&receiver);
    for (unsigned byte = 0U; byte < 256U; ++byte)
    {
        if (byte != 0x0FU)
        {
            assert(!push(&receiver, (uint8_t)byte, &out));
            assert(receiver.length == 0U);
        }
    }
    for (size_t n = 0U; n < 100U; ++n)
        check_frame(&receiver, frame); /* Back-to-back frames. */
}

static void test_payload_and_flags(void)
{
    SbusReceiver receiver;
    uint8_t bytes[SBUS_FRAME_SIZE];
    memcpy(bytes, frame, sizeof(bytes));
    /* A header-looking byte at every payload position must not restart reception. */
    for (size_t i = 1U; i <= 22U; ++i)
        bytes[i] = 0x0FU;
    for (unsigned flags = 0U; flags < 16U; ++flags)
    {
        bytes[23] = (uint8_t)flags;
        sbus_receiver_init(&receiver);
        check_frame(&receiver, bytes);
    }
}

static void test_resynchronization(void)
{
    SbusReceiver receiver;
    SbusFrame out = {0};
    uint8_t good[SBUS_FRAME_SIZE];
    memset(good, 0xFF, sizeof(good));
    good[0] = 0x0F;
    good[23] = 0;
    good[24] = 0;

    /* Real frame starts at each possible offset within a rejected candidate.
     * Prefix 0xFF ensures wrong alignments have invalid footer or flags.
     */
    for (size_t offset = 1U; offset < SBUS_FRAME_SIZE; ++offset)
    {
        sbus_receiver_init(&receiver);
        assert(!push(&receiver, 0x0F, &out));
        for (size_t i = 1U; i < offset; ++i)
            assert(!push(&receiver, 0xFF, &out));
        check_frame(&receiver, good);
    }

    /* Repeated false headers: exercise repeated overlapping memmove. */
    sbus_receiver_init(&receiver);
    for (size_t i = 0U; i < 100U; ++i)
        assert(!push(&receiver, 0x0F, &out));
    for (size_t i = 0U; i < 25U; ++i)
        assert(!push(&receiver, 0xFF, &out));
    check_frame(&receiver, good);

    /* Rejected footer / reserved flags, followed immediately by a good frame. */
    for (size_t bad_index = 23U; bad_index <= 24U; ++bad_index)
    {
        uint8_t bad[SBUS_FRAME_SIZE];
        memcpy(bad, frame, sizeof(bad));
        bad[bad_index] = 0xFF;
        sbus_receiver_init(&receiver);
        for (size_t i = 0U; i < sizeof(bad); ++i)
            assert(!push(&receiver, bad[i], &out));
        check_frame(&receiver, good);
    }

    /* Deleted or inserted byte in a nonzero-payload frame, then a known good frame.
     * These chosen vectors avoid false format-valid alignments; this is not a
     * guarantee that arbitrary corrupt SBUS data can always be detected.
     */
    for (size_t pos = 0U; pos < SBUS_FRAME_SIZE; ++pos)
    {
        uint8_t bad[SBUS_FRAME_SIZE];
        memset(bad, 0xFF, sizeof(bad));
        bad[0] = 0x0F;
        bad[23] = 0;
        bad[24] = 0;
        for (unsigned insert = 0U; insert < 2U; ++insert)
        {
            sbus_receiver_init(&receiver);
            for (size_t i = 0U; i < sizeof(bad); ++i)
            {
                if (insert && i == pos)
                    (void)push(&receiver, 0xFF, &out);
                if (insert || i != pos)
                    (void)push(&receiver, bad[i], &out);
            }
            check_frame(&receiver, good);
        }
    }
}

/* No-CRC limitation: noise plus a partial real frame can pass format checks. */
static void test_false_candidate_limit(void)
{
    uint8_t candidate[SBUS_FRAME_SIZE] = {0x0F, 0xFF, 0xFF};
    memcpy(&candidate[3], frame, SBUS_FRAME_SIZE - 3U);
    SbusReceiver receiver;
    sbus_receiver_init(&receiver);
    check_frame(&receiver, candidate);
}

static void test_reset_and_parameters(void)
{
    SbusReceiver receiver;
    SbusFrame out = {0};
    sbus_receiver_init(NULL);
    sbus_receiver_init(&receiver);
    assert(!push(&receiver, 0x0F, &out));
    unsigned char before[sizeof(receiver)];
    memcpy(before, &receiver, sizeof(receiver));
    assert(!sbus_receiver_push_byte(&receiver, 0x55, NULL));
    assert(memcmp(before, &receiver, sizeof(receiver)) == 0);
    unsigned char output_before[sizeof(out)];
    memcpy(output_before, &out, sizeof(out));
    assert(!sbus_receiver_push_byte(NULL, 0x55, &out));
    assert(memcmp(output_before, &out, sizeof(out)) == 0);
    assert(!sbus_receiver_push_byte(NULL, 0x55, NULL));

    sbus_receiver_init(&receiver); /* Drop a partial frame. */
    assert(receiver.length == 0U);
    check_frame(&receiver, frame);
    const size_t invalid_lengths[] = {SBUS_FRAME_SIZE, SBUS_FRAME_SIZE + 1U, (size_t)-1};
    for (size_t i = 0U; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); ++i)
    {
        receiver.length = invalid_lengths[i]; /* Deliberate fault injection. */
        check_frame(&receiver, frame);
    }
}

/* Independent offline reference: try every possible start, consume 25 bytes
 * only after acceptance. Compare exact output timing on a deterministic stream.
 */
static void test_reference_stream(void)
{
    uint8_t stream[8192];
    bool expected_ready[sizeof(stream)] = {false};
    uint32_t random_state = 1U;
    for (size_t i = 0U; i < sizeof(stream); ++i)
    {
        random_state = random_state * 1664525U + 1013904223U;
        stream[i] = (uint8_t)(random_state >> 24);
    }
    for (size_t i = 37U; i + SBUS_FRAME_SIZE <= sizeof(stream); i += 97U)
        memcpy(&stream[i], frame, sizeof(frame));

    SbusFrame expected;
    for (size_t start = 0U; start + SBUS_FRAME_SIZE <= sizeof(stream);)
    {
        if (sbus_decode_frame(&stream[start], SBUS_FRAME_SIZE, &expected))
        {
            expected_ready[start + SBUS_FRAME_SIZE - 1U] = true;
            start += SBUS_FRAME_SIZE;
        }
        else
            ++start;
    }
    SbusReceiver receiver;
    SbusFrame actual = {0};
    sbus_receiver_init(&receiver);
    for (size_t i = 0U; i < sizeof(stream); ++i)
    {
        assert(push(&receiver, stream[i], &actual) == expected_ready[i]);
        if (expected_ready[i])
        {
            assert(sbus_decode_frame(&stream[i + 1U - SBUS_FRAME_SIZE], SBUS_FRAME_SIZE, &expected));
            check_equal(&actual, &expected);
        }
    }
}

int main(void)
{
    test_chunks_and_noise();
    test_payload_and_flags();
    test_resynchronization();
    test_reset_and_parameters();
    test_false_candidate_limit();
    test_reference_stream();
    puts("PASS: SBUS receiver chunks, consecutive frames, resync, flags, reset, bounds and reference stream");
    return 0;
}

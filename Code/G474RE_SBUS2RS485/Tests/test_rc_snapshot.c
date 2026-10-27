/* Windows host only; models exclusion, not Cortex-M interrupt timing. */
#include "rc_snapshot.h"
#include "rc_snapshot_test_port.h"
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#ifdef NDEBUG
#error "Assertions must remain enabled."
#endif

static CRITICAL_SECTION mutex;
static _Thread_local uint32_t mock_primask;
static volatile LONG lock_calls;
static HANDLE start_event;

uint32_t __get_PRIMASK(void) { return mock_primask; }
void __disable_irq(void)
{
    if (mock_primask == 0U) EnterCriticalSection(&mutex);
    mock_primask = 1U;
    InterlockedIncrement(&lock_calls);
}
void __set_PRIMASK(uint32_t mask)
{
    assert(mock_primask == 1U && mask <= 1U);
    mock_primask = mask;
    if (mask == 0U) LeaveCriticalSection(&mutex);
}
void __DMB(void) { __sync_synchronize(); }

static SbusFrame make_frame(uint32_t value)
{
    SbusFrame frame = {0};
    for (size_t ch = 0U; ch < SBUS_CHANNEL_COUNT; ++ch)
        frame.channels[ch] = (uint16_t)((value + ch) & 0x07FFU);
    frame.frame_lost = (value & 1U) != 0U;
    frame.failsafe = (value & 2U) != 0U;
    return frame;
}

static void check_frame(const SbusFrame *a, const SbusFrame *b)
{
    for (size_t ch = 0U; ch < SBUS_CHANNEL_COUNT; ++ch)
        assert(a->channels[ch] == b->channels[ch]);
    assert(a->frame_lost == b->frame_lost && a->failsafe == b->failsafe);
}

static void test_basics(void)
{
    RcSnapshot out;
    const SbusFrame zero = {0};
    rc_snapshot_init();
    assert(rc_snapshot_read(&out));
    check_frame(&out.frame, &zero);
    assert(!out.has_frame && !out.transport_ok && out.sequence == 0U && out.received_ms == 0U);
    LONG before = lock_calls;
    assert(!rc_snapshot_publish(NULL, 123U));
    assert(!rc_snapshot_read(NULL));
    assert(lock_calls == before);

    for (uint32_t flags = 0U; flags < 4U; ++flags)
    {
        SbusFrame input = make_frame(flags);
        const SbusFrame expected = input;
        before = lock_calls;
        assert(rc_snapshot_publish(&input, 100U + flags));
        assert(lock_calls == before + 1);
        input.channels[0] = 2047U; /* Must not retain input pointer. */
        assert(rc_snapshot_read(&out));
        assert(lock_calls == before + 2);
        check_frame(&out.frame, &expected);
        assert(out.has_frame && out.transport_ok && out.sequence == flags + 1U);
        assert(out.received_ms == 100U + flags);
        out.frame.channels[0] = 999U; /* Reader owns a separate copy. */
        assert(!rc_snapshot_publish(NULL, 0U));
        assert(rc_snapshot_read(&out));
        check_frame(&out.frame, &expected);
        assert(out.sequence == flags + 1U && out.received_ms == 100U + flags);
        assert(mock_primask == 0U);
    }
    const SbusFrame input = make_frame(7U);
    assert(rc_snapshot_publish(&input, UINT32_MAX));
    assert(rc_snapshot_read(&out) && out.received_ms == UINT32_MAX);
    assert(rc_snapshot_publish(&input, 0U));
    assert(rc_snapshot_read(&out) && out.received_ms == 0U && out.sequence == 6U);
    rc_snapshot_invalidate();
    assert(rc_snapshot_read(&out));
    assert(out.has_frame && !out.transport_ok && out.sequence == 6U && out.received_ms == 0U);
    check_frame(&out.frame, &input);
    assert(rc_snapshot_publish(&input, 20U));
    assert(rc_snapshot_read(&out) && out.transport_ok && out.sequence == 7U);

    __disable_irq(); /* Nested calls must not accidentally enable interrupts. */
    rc_snapshot_init();
    assert(mock_primask == 1U);
    assert(rc_snapshot_read(&out));
    assert(mock_primask == 1U);
    check_frame(&out.frame, &zero);
    assert(!out.has_frame && out.sequence == 0U && out.received_ms == 0U);
    assert(rc_snapshot_publish(&input, 50U));
    assert(mock_primask == 1U);
    rc_snapshot_invalidate();
    assert(mock_primask == 1U);
    __set_PRIMASK(0U);
}

#define ITERATIONS 30000U
static DWORD WINAPI writer(LPVOID ignored)
{
    (void)ignored;
    assert(WaitForSingleObject(start_event, INFINITE) == WAIT_OBJECT_0);
    for (uint32_t i = 1U; i <= ITERATIONS; ++i)
    {
        const SbusFrame input = make_frame(i);
        assert(rc_snapshot_publish(&input, i));
        assert(mock_primask == 0U);
    }
    return 0;
}

static DWORD WINAPI reader(LPVOID ignored)
{
    (void)ignored;
    assert(WaitForSingleObject(start_event, INFINITE) == WAIT_OBJECT_0);
    uint32_t previous = 0U;
    for (uint32_t i = 0U; i < ITERATIONS; ++i)
    {
        RcSnapshot out;
        assert(rc_snapshot_read(&out));
        assert(mock_primask == 0U);
        if (out.has_frame)
        {
            assert(out.transport_ok);
            assert(out.sequence >= previous && out.received_ms == out.sequence);
            const SbusFrame expected = make_frame(out.sequence);
            check_frame(&out.frame, &expected);
            previous = out.sequence;
        }
        else assert(out.sequence == 0U && out.received_ms == 0U);
    }
    return 0;
}

int main(void)
{
    InitializeCriticalSection(&mutex);
    test_basics();
    rc_snapshot_init();
    start_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(start_event != NULL);
    HANDLE threads[3];
    threads[0] = CreateThread(NULL, 0, writer, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    for (size_t i = 0U; i < 3U; ++i) assert(threads[i] != NULL);
    assert(SetEvent(start_event));
    assert(WaitForMultipleObjects(3, threads, TRUE, 30000) == WAIT_OBJECT_0);
    RcSnapshot out;
    assert(rc_snapshot_read(&out) && out.sequence == ITERATIONS);
    for (size_t i = 0U; i < 3U; ++i) assert(CloseHandle(threads[i]));
    assert(CloseHandle(start_event));
    DeleteCriticalSection(&mutex);
    puts("PASS: snapshot initialization, copies, flags, NULL, timestamps, nested lock and concurrent consistency");
    return 0;
}

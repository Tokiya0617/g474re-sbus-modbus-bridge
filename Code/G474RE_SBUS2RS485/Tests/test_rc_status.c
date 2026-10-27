/* Host-only tests. Do not add this main() to the STM32 firmware target. */
#include "rc_status.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "These tests require assert(); do not define NDEBUG."
#endif

/* 以下毫秒值均为合成测试输入，不是 GR01 实测周期或获批失联阈值。 */
static void test_combinations(void)
{
    const uint32_t timeouts[] = {0U, 1U, 100U, (uint32_t)INT32_MAX,
                                 (uint32_t)INT32_MAX + 1U, UINT32_MAX};
    const uint32_t ages[] = {0U, 1U, 99U, 100U, 101U,
                             (uint32_t)INT32_MAX, UINT32_MAX};
    const uint32_t received_times[] = {0U, UINT32_MAX - 50U};
    size_t cases = 0U;

    for (unsigned bits = 0U; bits < 16U; ++bits)
    {
        for (size_t t = 0U; t < sizeof(timeouts) / sizeof(timeouts[0]); ++t)
        {
            for (size_t a = 0U; a < sizeof(ages) / sizeof(ages[0]); ++a)
            {
                for (size_t r = 0U; r < sizeof(received_times) / sizeof(received_times[0]); ++r)
                {
                    RcSnapshot snapshot = {0};
                    RcStatus out;
                    snapshot.has_frame = (bits & 1U) != 0U;
                    snapshot.transport_ok = (bits & 2U) != 0U;
                    snapshot.frame.frame_lost = (bits & 4U) != 0U;
                    snapshot.frame.failsafe = (bits & 8U) != 0U;
                    snapshot.received_ms = received_times[r];
                    snapshot.sequence = 0U; /* 序号回绕到 0 不代表没有帧。 */
                    for (size_t ch = 0U; ch < SBUS_CHANNEL_COUNT; ++ch)
                    {
                        snapshot.frame.channels[ch] = (uint16_t)(ch * 127U);
                    }

                    unsigned char before[sizeof(snapshot)];
                    memcpy(before, &snapshot, sizeof(snapshot));
                    const uint32_t now_ms = snapshot.received_ms + ages[a];
                    assert(rc_status_evaluate(&snapshot, now_ms, timeouts[t], &out));
                    assert(memcmp(before, &snapshot, sizeof(snapshot)) == 0);

                    const bool configured = timeouts[t] > 0U &&
                                            timeouts[t] <= (uint32_t)INT32_MAX;
                    const bool timed_out = snapshot.has_frame && configured &&
                                           ages[a] >= timeouts[t];
                    /* 独立枚举健康输入：仅 bits == 3 表示有帧且传输正常。 */
                    const bool usable = bits == 3U && configured &&
                                        ages[a] < timeouts[t];
                    assert(out.has_frame == snapshot.has_frame);
                    assert(out.timeout_configured == configured);
                    assert(out.age_ms == (snapshot.has_frame ? ages[a] : UINT32_MAX));
                    assert(out.transport_ok == (snapshot.has_frame && snapshot.transport_ok));
                    assert(out.frame_lost == (snapshot.has_frame && snapshot.frame.frame_lost));
                    assert(out.failsafe == (snapshot.has_frame && snapshot.frame.failsafe));
                    assert(out.timed_out == timed_out);
                    assert(out.usable == usable);
                    ++cases;
                }
            }
        }
    }
    assert(cases == 1344U);
}

static void test_boundaries_and_wrap(void)
{
    RcSnapshot snapshot = {0};
    RcStatus out;
    snapshot.has_frame = true;
    snapshot.transport_ok = true;
    snapshot.received_ms = UINT32_MAX - 10U;

    assert(rc_status_evaluate(&snapshot, 88U, 100U, &out));
    assert(out.age_ms == 99U && out.usable && !out.timed_out);
    assert(rc_status_evaluate(&snapshot, 89U, 100U, &out));
    assert(out.age_ms == 100U && !out.usable && out.timed_out);
    assert(rc_status_evaluate(&snapshot, 90U, 100U, &out));
    assert(out.age_ms == 101U && !out.usable && out.timed_out);

    snapshot.received_ms = 0U;
    assert(rc_status_evaluate(&snapshot, 0U, 1U, &out));
    assert(out.age_ms == 0U && out.usable);
    assert(rc_status_evaluate(&snapshot, 1U, 1U, &out));
    assert(out.timed_out && !out.usable);

    /* now 错用为接收之前的时刻，在这个向量下会保守表现为超时。 */
    snapshot.received_ms = 100U;
    assert(rc_status_evaluate(&snapshot, 99U, 100U, &out));
    assert(out.age_ms == UINT32_MAX && out.timed_out && !out.usable);
}

static void test_state_changes(void)
{
    RcSnapshot snapshot = {0};
    RcStatus out;
    assert(rc_status_evaluate(&snapshot, 1000U, 100U, &out));
    assert(!out.has_frame && !out.usable && !out.timed_out);

    snapshot.has_frame = true;
    snapshot.transport_ok = true;
    snapshot.received_ms = 1000U;
    snapshot.frame.channels[0] = 1234U;
    assert(rc_status_evaluate(&snapshot, 1000U, 0U, &out));
    assert(!out.timeout_configured && !out.usable && !out.timed_out);
    assert(rc_status_evaluate(&snapshot, 1000U, 100U, &out));
    assert(out.usable);

    /* 模拟 invalidate 后仍保留历史帧：年龄很小也不能通过。 */
    snapshot.transport_ok = false;
    assert(rc_status_evaluate(&snapshot, 1001U, 100U, &out));
    assert(!out.transport_ok && !out.usable && !out.timed_out);
    /* 不改快照再次计算，模拟仅 UART 重启、尚无新帧。 */
    assert(rc_status_evaluate(&snapshot, 1002U, 100U, &out));
    assert(!out.usable);

    snapshot.received_ms = 1010U;
    snapshot.transport_ok = true;
    assert(rc_status_evaluate(&snapshot, 1011U, 100U, &out));
    assert(out.usable);
    snapshot.frame.frame_lost = true;
    assert(rc_status_evaluate(&snapshot, 1012U, 100U, &out));
    assert(out.frame_lost && !out.usable);
    snapshot.frame.frame_lost = false;
    snapshot.frame.failsafe = true;
    assert(rc_status_evaluate(&snapshot, 1013U, 100U, &out));
    assert(out.failsafe && !out.usable);
    snapshot.frame.failsafe = false;
    assert(rc_status_evaluate(&snapshot, 1110U, 100U, &out));
    assert(out.timed_out && !out.usable);

    /* 新帧解除超时；纯计算函数不锁存之前的失败结果。 */
    snapshot.received_ms = 1110U;
    assert(rc_status_evaluate(&snapshot, 1111U, 100U, &out));
    assert(out.usable && snapshot.frame.channels[0] == 1234U);
}

static void test_null_arguments(void)
{
    const RcSnapshot snapshot = {0};
    RcStatus out = {0};
    out.age_ms = 456U;
    out.has_frame = true;
    unsigned char before[sizeof(out)];
    memcpy(before, &out, sizeof(out));

    assert(!rc_status_evaluate(NULL, 0U, 100U, &out));
    assert(memcmp(before, &out, sizeof(out)) == 0);
    assert(!rc_status_evaluate(&snapshot, 0U, 100U, NULL));
    assert(!rc_status_evaluate(NULL, 0U, 100U, NULL));
}

int main(void)
{
    test_combinations();
    test_boundaries_and_wrap();
    test_state_changes();
    test_null_arguments();
    puts("PASS: 1344 status cases, timeout boundaries/wrap, recovery, NULL and unchanged input");
    return 0;
}

#ifndef REGISTER_MAP_H
#define REGISTER_MAP_H
#include "rc_status.h"

#define BRIDGE_REGISTER_COUNT 28U
#define BRIDGE_PROTOCOL_VERSION 1U /* 本候选寄存器布局的版本，不表示 PLC 已确认。 */
typedef enum { REGISTER_WORD_ORDER_UNSET, REGISTER_HIGH_WORD_FIRST,
               REGISTER_LOW_WORD_FIRST } RegisterWordOrder;

/* 仅为本工程候选位布局，必须与 PLC 协议一同批准，不是 Modbus 标准状态位。 */
#define RC_REG_USABLE           (1U << 0)
#define RC_REG_FRAME_LOST       (1U << 1)
#define RC_REG_FAILSAFE         (1U << 2)
#define RC_REG_TIMEOUT          (1U << 3)
#define RC_REG_TRANSPORT_FAULT  (1U << 4)
#define RC_REG_NO_FRAME         (1U << 5)
#define RC_REG_UNCONFIGURED     (1U << 6)
#define RC_REG_COMM_FAULT       (1U << 7)

typedef struct
{
    uint32_t valid_sbus_frames; /* 已发布且未置 frame_lost/failsafe 的帧计数。 */
    uint32_t rejected_candidates; /* 收齐 25 字节但解码不通过的候选数，非坏字节数。 */
    uint32_t modbus_requests;   /* 本站 CRC 合法请求数，包括异常请求。 */
    uint16_t firmware_version;
    bool comm_fault;
} RegisterDiagnostics;

/* snapshot 为一致副本；status 须对应同一副本；诊断计数是独立采样，不保证与该帧同一瞬间。
 * out 至少 28 个 uint16_t，不得与输入重叠。失败不改 out。
 * age 为最新结构合法帧年龄，65535 饱和；无帧也为 65535，须同时检查状态。
 * sequence 仅保留低 16 位；uint32 计数按调用者显式选择的字序拆分。
 * 不清零无效通道，不把旧值替换成虚构安全值；PLC 必须检查状态/序号/年龄。 */
bool register_map_build(const RcSnapshot *snapshot, const RcStatus *status,
                        RegisterWordOrder order,
                        const RegisterDiagnostics *diagnostics,
                        uint16_t out[BRIDGE_REGISTER_COUNT]);
#endif

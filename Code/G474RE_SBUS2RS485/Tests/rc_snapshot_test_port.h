#ifndef RC_SNAPSHOT_TEST_PORT_H
#define RC_SNAPSHOT_TEST_PORT_H
#if !defined(RC_SNAPSHOT_HOST_TEST) || defined(__arm__) || defined(__thumb__)
#error "Only for explicitly enabled host tests, never Cortex-M firmware."
#endif
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void __DMB(void);
#endif

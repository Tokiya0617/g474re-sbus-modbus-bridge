#ifndef BRIDGE_SERVICE_TEST_PORT_H
#define BRIDGE_SERVICE_TEST_PORT_H
#if !defined(BRIDGE_SERVICE_HOST_TEST) || defined(__arm__) || defined(__thumb__)
#error "Host-only bridge service port."
#endif
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t saved);
void __DMB(void);
#endif

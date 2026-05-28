// Physical memory parameters

#ifndef RAM_SIZE
#ifndef RAM_SIZE_MB
#define RAM_SIZE ((size_t)16 * 1024 * 1024)
#else
#define RAM_SIZE ((size_t)RAM_SIZE_MB * 1024 * 1024)
#endif
#endif

#define RAM_START_PMA 0x80000000UL
#define RAM_START ((void*)RAM_START_PMA)
#define RAM_END_PMA (RAM_START_PMA + RAM_SIZE)
#define RAM_END (RAM_START + RAM_SIZE)

// Number of external interrupt sources

#ifndef NIRQ
#define NIRQ PLIC_SRC_CNT
#endif

// Interrupt priorities

#define UART_INTR_PRIO 3
#define VIOBLK_INTR_PRIO 1
#define VIOCONS_INTR_PRIO 2
#define VIORNG_INTR_PRIO 1
#define VIOGPU_INTR_PRIO 2
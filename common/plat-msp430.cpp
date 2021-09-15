#include "Tools/portable.h"
#ifdef __TOOLS_MSP__
#include <driverlib.h>
#endif
#ifdef __MSP430__
#include <msp430.h>
#include <DSPLib.h>
#include "main.h"
#elif defined(__MSP432__)
#include <msp432.h>
#endif
#include <cstdint>
#include <cstring>
#include "intermittent-cnn.h"
#include "cnn_common.h"
#include "platform.h"
#include "platform-private.h"
#include "data.h"
#include "my_debug.h"
#include "plat-msp430.h"
#include "Tools/myuart.h"
#include "Tools/our_misc.h"
#include "Tools/dvfs.h"
#include "Tools/ext_fram/extfram.h"

#ifdef __MSP430__
#pragma DATA_SECTION(".nvm2")
#endif
static Counters counters_data[COUNTERS_LEN];
Counters *counters(uint16_t idx) {
    return counters_data + idx;
}

#ifdef __MSP430__

#define MY_DMA_CHANNEL DMA_CHANNEL_0

#endif

void my_memcpy(void* dest, const void* src, size_t n) {
#ifdef __MSP430__
    DMA0CTL = 0;

    DMACTL0 &= 0xFF00;
    // set DMA transfer trigger for channel 0
    DMACTL0 |= DMA0TSEL__DMAREQ;

    DMA_setSrcAddress(MY_DMA_CHANNEL, (uint32_t)src, DMA_DIRECTION_INCREMENT);
    DMA_setDstAddress(MY_DMA_CHANNEL, (uint32_t)dest, DMA_DIRECTION_INCREMENT);
    /* transfer size is in words (2 bytes) */
    DMA0SZ = n >> 1;
    DMA0CTL |= DMAEN + DMA_TRANSFER_BLOCK + DMA_SIZE_SRCWORD_DSTWORD;
    DMA0CTL |= DMAREQ;
#elif defined(__MSP432__)
    MAP_DMA_enableModule();
    MAP_DMA_setControlBase(controlTable);
    MAP_DMA_setChannelControl(
        DMA_CH0_RESERVED0 | UDMA_PRI_SELECT, // Channel 0, PRImary channel
        // re-arbitrate after 1024 (maximum) items
        // an item is 16-bit
        UDMA_ARB_1024 | UDMA_SIZE_16 | UDMA_SRC_INC_16 | UDMA_DST_INC_16
    );
    // Use the first configurable DMA interrupt handler DMA_INT1_IRQHandler,
    // which is defined below (overriding weak symbol in startup*.c)
    MAP_DMA_assignInterrupt(DMA_INT1, 0);
    MAP_Interrupt_enableInterrupt(INT_DMA_INT1);
    MAP_Interrupt_disableSleepOnIsrExit();
    MAP_DMA_setChannelTransfer(
        DMA_CH0_RESERVED0 | UDMA_PRI_SELECT,
        UDMA_MODE_AUTO, // Set as auto mode with no need to retrigger after each arbitration
        const_cast<void*>(src), dest,
        n >> 1 // transfer size in items
    );
    curDMATransmitChannelNum = 0;
    MAP_DMA_enableChannel(0);
    MAP_DMA_requestSoftwareTransfer(0);
    while (MAP_DMA_isChannelEnabled(0)) {}
#endif
}

void read_from_nvm(void* vm_buffer, uint32_t nvm_offset, size_t n) {
    SPI_ADDR addr;
    addr.L = nvm_offset;
    SPI_READ(&addr, reinterpret_cast<uint8_t*>(vm_buffer), n);
}

void write_to_nvm(const void* vm_buffer, uint32_t nvm_offset, size_t n, uint16_t timer_delay) {
    SPI_ADDR addr;
    addr.L = nvm_offset;
    check_nvm_write_address(nvm_offset, n);
    MY_ASSERT(n <= 1024);
    SPI_WRITE2(&addr, reinterpret_cast<const uint8_t*>(vm_buffer), n, timer_delay);
    if (!timer_delay) {
        SPI_WAIT_DMA();
    }
}

uint64_t get_nvm_writes(void) {
    return 0;
}

void my_erase() {
    eraseFRAM2(0x00);
}

void copy_samples_data(void) {
    write_to_nvm_segmented(samples_data, SAMPLES_OFFSET, SAMPLES_DATA_LEN);
}

[[ noreturn ]] void ERROR_OCCURRED(void) {
    while (1);
}

#ifdef __MSP430__
#define GPIO_COUNTER_PORT GPIO_PORT_P8
#define GPIO_COUNTER_PIN GPIO_PIN0
#define GPIO_RESET_PORT GPIO_PORT_P5
#define GPIO_RESET_PIN GPIO_PIN7
#elif defined(__MSP432__)
#define GPIO_COUNTER_PORT GPIO_PORT_P5
#define GPIO_COUNTER_PIN GPIO_PIN5
#define GPIO_RESET_PORT GPIO_PORT_P2
#define GPIO_RESET_PIN GPIO_PIN5
#endif

#define STABLE_POWER_ITERATIONS 10

void IntermittentCNNTest() {
#ifdef __TOOLS_MSP__
    GPIO_setAsOutputPin(GPIO_COUNTER_PORT, GPIO_COUNTER_PIN);
    GPIO_setOutputLowOnPin(GPIO_COUNTER_PORT, GPIO_COUNTER_PIN);
    GPIO_setAsInputPinWithPullUpResistor(GPIO_RESET_PORT, GPIO_RESET_PIN);

    // sleep to wait for external FRAM
    // 5ms / (1/f)
    // XXX: seems not needed on STM32(?)
    our_delay_cycles(5E-3 * getFrequency(FreqLevel));
#endif

    initSPI();
    if (testSPI() != 0) {
        // external FRAM failed to initialize - reset
        volatile uint16_t counter = 1000;
        // waiting some time seems to increase the possibility
        // of a successful FRAM initialization on next boot
        while (counter--);
#ifdef __TOOLS_MSP__
        WDTCTL = 0;
#else
        while (1) {} // TODO
#endif
    }

    Model* model = load_model_from_nvm();
#ifdef __TOOLS_MSP__
    if (!GPIO_getInputPinValue(GPIO_RESET_PORT, GPIO_RESET_PIN))
#endif
    {
        uartinit();

        my_printf(NEWLINE "run_counter = %d" NEWLINE, model->run_counter);

        first_run();

        notify_model_finished();

        for (uint8_t idx = 0; idx < STABLE_POWER_ITERATIONS; idx++) {
            run_cnn_tests(1);
        }

        my_printf("Done testing run" NEWLINE);

        while (1);
    }

    while (1) {
        run_cnn_tests(1);
    }
}

void button_pushed(uint16_t button1_status, uint16_t button2_status) {
    my_printf_debug("button1_status=%d button2_status=%d" NEWLINE, button1_status, button2_status);
}

void notify_model_finished(void) {
    my_printf("." NEWLINE);
#ifdef __TOOLS_MSP__
    GPIO_toggleOutputOnPin(GPIO_COUNTER_PORT, GPIO_COUNTER_PIN);
#endif
}

void start_cpu_counter(void) {
#if defined(__MSP430__) && ENABLE_COUNTERS
    msp_benchmarkStart(MSP_BENCHMARK_BASE, 1);
#endif
}

void stop_cpu_counter(uint32_t Counters::* mem_ptr) {
#if defined(__MSP430__) && ENABLE_COUNTERS
    counters(get_model()->layer_idx)->*mem_ptr += msp_benchmarkStop(MSP_BENCHMARK_BASE);
#endif
}

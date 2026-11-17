# Multitasking with the Salvo RTOS on PIC24

A small embedded C project that runs four cooperative tasks on a PIC24 microcontroller under the [Salvo RTOS](http://www.pumpkininc.com). It shows task scheduling, delays, semaphore-protected shared resources, ADC sampling, UART logging and a timer interrupt driving the RTOS tick.

## What it does

| Task | Priority | What it does | Delay |
|------|----------|--------------|-------|
| `TaskKnightRider` | 5 | Sweeps a Knight Rider pattern across LEDs on RA0–RA6. A debounced button press on RB0 reverses the direction. | 30 ticks |
| `TaskRunLed` | 5 | Blinks the run LED on RA7: off for 300 ticks, on for 100 ticks. | 300 / 100 ticks |
| `TaskTempUart` | 8 | Reads the temperature sensor on AN4 and prints it over UART2. | 50 ticks |
| `TaskPotUart` | 8 | Reads the potentiometer on AN5 and prints the voltage over UART2. | 50 ticks |

Delays are in RTOS ticks. The comments in the code assume a 10 ms tick, so 30 ticks is 300 ms and 300 ticks is 3 s (see [Notes](#notes)).

## Semaphores

Four binary semaphores protect shared resources and signal events:

| Semaphore | Purpose |
|-----------|---------|
| `UART_MUTEX` | One task at a time writes to UART2 |
| `ADC_MUTEX` | One task at a time uses the ADC |
| `LED_MUTEX` | Protects the shared LED port (PORTA) |
| `BUTTON_EVENT_SEM` | Signalled when the button is pressed |

## Hardware

- PIC24 development board, 8 MHz crystal (`POSCMOD = XT`) <!-- TODO: add the exact part number -->
- 8 LEDs on RA0–RA7
- Push button on RB0, active low
- TC1047A temperature sensor on AN4 (10 mV/°C, 500 mV offset at 0 °C)
- Potentiometer on AN5
- Serial connection on UART2 (8-N-1) to a terminal on your PC

## Software

- Salvo RTOS (library build, see `salvocfg.h`)
- MPLAB XC16 compiler <!-- TODO: confirm the MPLAB X version you used -->

## Files

| File | Purpose |
|------|---------|
| `RtosMain.c` | Configuration bits, tasks, `main()`, Timer1 ISR and peripheral setup |
| `salvocfg.h` | Salvo configuration (library type, number of tasks and events) |
| `salvomem.c` | Salvo memory and global variable definitions |

## How it works

1. `main()` initialises the oscillator, pins, ADC, UART2 and Timer1, then calls `OSInit()`.
2. It creates the four semaphores and the four tasks, and prints a startup banner over UART.
3. The main loop calls `OSSched()` forever. Each task runs until it waits or delays, then hands control back to the scheduler.
4. The Timer1 interrupt counts up to 10 and then calls `OSTimer()`, which advances every task delay by one tick.
5. Each sensor task takes the ADC semaphore to sample, updates the shared `g_SensorData` structure, then takes the UART semaphore to print.

## Build and run

1. Create an MPLAB X project for your PIC24 device and add `RtosMain.c`, `salvocfg.h` and `salvomem.c`.
2. Add the Salvo library for the XC16 compiler to the project.
3. Build and program the board.
4. Open a serial terminal on the UART2 port. The baud rate is set by `U2BRG = 0x0019`, which the code comments describe as about 19200.

## Example output

```
=== RTOS Multi-Task System ===
4 Semaphores Active:
1. UART Mutex
2. ADC Mutex
3. LED Mutex
4. Button Event Signal
=============================

[TEMP] 24.30 C (ADC:240)
[POT] 1.65 V (ADC:512) | Samples:1
```

The values above are illustrative. Replace them with a capture from your own board.

## Notes

- **Tick timing:** Timer1 uses a 1:8 prescaler and `PR1 = 0x0FA0` (4000). With an 8 MHz crystal and no PLL that gives an interrupt about every 8 ms, not 1 ms as the comment says, so one RTOS tick would be about 80 ms. Measure the tick on a scope or with a toggled pin and adjust `PR1` or the divider so the delays match the comments.
- **Salvo configuration:** `salvocfg.h` sets `OSTASKS` to 3 and `OSEVENTS` to 1, but the code creates 4 tasks and 4 semaphores. Raise both values to 4 and enable binary semaphores if they are not already enabled.
- **Cooperative scheduling:** Salvo switches tasks only at waits and delays, so the mutexes matter only where a task could yield while holding a resource.
- **Shared data:** `g_SensorData` is written by both sensor tasks without a semaphore. This is safe under cooperative scheduling but worth guarding if you move to a preemptive RTOS.
- **Sample counter:** `sample_count` is a `uint8_t`, so it wraps at 256 and the "every 10 samples" separator drifts after the wrap.
- **Button event:** `BUTTON_EVENT_SEM` is signalled but no task waits on it yet.

## Author

Aryan Prajapati, Computer Engineering Technology, Sheridan College
[LinkedIn](https://www.linkedin.com/in/aryan-prajapati-5ab386262/) | [GitHub](https://github.com/Aryan-1912)

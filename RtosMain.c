#include <xc.h>
#include <salvo.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <libpic30.h>

#define _XTAL_FREQ 8000000UL

// ================= CONFIG BITS =================
#pragma config POSCMOD = XT
#pragma config OSCIOFNC = OFF
#pragma config FCKSM   = CSDCMD
#pragma config FNOSC   = PRI
#pragma config IESO    = ON

#pragma config WDTPS = PS32768
#pragma config FWPSA = PR128
#pragma config WINDIS = ON
#pragma config FWDTEN = OFF
#pragma config ICS    = PGx2
#pragma config BKBUG  = OFF
#pragma config GWRP   = OFF
#pragma config GCP    = OFF
#pragma config JTAGEN = OFF

// ================= PROTOTYPES =================
void SYSTEM_Initialize(void);
void OSCILLATOR_Initialize(void);
void PIN_MANAGER_Initialize(void);
void TMR1_Initialize(void);
void UART2_Initialize(void);
void ADC1_Initialize(void);

void TaskKnightRider(void);
void TaskRunLed(void);
void TaskTempUart(void);
void TaskPotUart(void);

uint16_t ADC1_Read(uint8_t ch);
float AdcToVoltage(uint16_t v);
float AdcToTempC(uint16_t v);
void UART2_WriteChar(char c);
void UART2_WriteString(const char *s);
bool ButtonPressed(void);

// ================= SALVO DEFINES ===============
#define TASK_KNIGHT_P OSTCBP(1)
#define TASK_RUNLED_P OSTCBP(2)
#define TASK_TEMP_P   OSTCBP(3)
#define TASK_POT_P    OSTCBP(4)

#define PRIO_KNIGHT 5
#define PRIO_RUNLED 5
#define PRIO_TEMP   8
#define PRIO_POT    8

#define TEMP_ADC_CH 4
#define POT_ADC_CH  5

// ============= SEMAPHORE DEFINITIONS =============
// Semaphore 1: UART Mutual Exclusion (Binary Semaphore)
#define UART_MUTEX OSECBP(1)

// Semaphore 2: ADC Resource Protection (Binary Semaphore)
#define ADC_MUTEX OSECBP(2)

// Semaphore 3: LED Resource Protection (Binary Semaphore)
#define LED_MUTEX OSECBP(3)

// Semaphore 4: Button Event Signal (Binary Semaphore)
#define BUTTON_EVENT_SEM OSECBP(4)

// ============= Shared Data Structure with Protection =============
typedef struct {
    float temperature;
    float voltage;
    uint16_t temp_raw_adc;
    uint16_t pot_raw_adc;
    uint8_t sample_count;
} SharedSensorData_t;

volatile SharedSensorData_t g_SensorData = {0};

// ============= Button  Function ========
bool ButtonPressed(void)
{
    static uint8_t debounce_count = 0;
    static bool last_stable_state = true;  // Released (active low)
    
    bool current_raw = (PORTB & 0x0001) ? true : false;
    
    if (current_raw == last_stable_state) {
        debounce_count = 0;
        return false;
    }
    
    debounce_count++;
    if (debounce_count >= 3) {  // 3 consecutive different readings
        debounce_count = 0;
        if (!current_raw && last_stable_state) {  // Pressed (active low)
            last_stable_state = current_raw;
            return true;
        }
        last_stable_state = current_raw;
    }
    
    return false;
}

// ============= TASK 1 ? Knight Rider with Semaphore Protection ========
void TaskKnightRider(void)
{
    uint8_t step    = 0;
    uint8_t dir     = 0;  // 0 = left-to-right, 1 = right-to-left

    const uint8_t leftSeq[7]  = {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F};
    const uint8_t rightSeq[7] = {0x40,0x60,0x70,0x78,0x7C,0x7E,0x7F};

    for (;;) {
        // Check for button press with debouncing
        if (ButtonPressed()) {
            // Signal button event to other tasks (if needed)
            OSSignalSem(BUTTON_EVENT_SEM);
            dir ^= 1;
            step = 0;
        }

        uint8_t mask = (dir == 0) ? leftSeq[step] : rightSeq[step];

        // Use LED mutex to protect shared LED resource (PORTA bits 0-6)
        OSWaitSem(LED_MUTEX, OSNO_TIMEOUT);
        LATA = (LATA & ~0x007F) | (mask & 0x007F);
        OSSignalSem(LED_MUTEX);

        step++;
        if (step >= 7) {
            step = 0;
            dir ^= 1;  // Auto-reverse at end
        }

        OS_Delay(30);  // 300ms delay
    }
}

// ============= TASK 2 ? RUN LED with Semaphore Protection =======
void TaskRunLed(void)
{
    for (;;) {
        // Use LED mutex to protect shared LED resource (PORTA bit 7)
        OSWaitSem(LED_MUTEX, OSNO_TIMEOUT);
        LATA &= ~0x0080;   // OFF
        OSSignalSem(LED_MUTEX);
        
        OS_Delay(300);     // 3 seconds (300 × 10ms)

        OSWaitSem(LED_MUTEX, OSNO_TIMEOUT);
        LATA |= 0x0080;    // ON
        OSSignalSem(LED_MUTEX);
        
        OS_Delay(100);     // 1 second (100 × 10ms)
    }
}

// ============= TASK 3 ? Temperature with Semaphores ========================
void TaskTempUart(void)
{
    for (;;) {
        // Protect ADC resource during conversion
        OSWaitSem(ADC_MUTEX, OSNO_TIMEOUT);
        uint16_t adc = ADC1_Read(TEMP_ADC_CH);
        OSSignalSem(ADC_MUTEX);
        
        float tC = AdcToTempC(adc);
        
        // Update shared sensor data structure
        g_SensorData.temperature = tC;
        g_SensorData.temp_raw_adc = adc;
        g_SensorData.sample_count++;

        char buf[50];
        sprintf(buf, "[TEMP] %.2f C (ADC:%u)\r\n", (double)tC, adc);
        
        // Protect UART resource during transmission
        OSWaitSem(UART_MUTEX, OSNO_TIMEOUT);
        UART2_WriteString(buf);
        OSSignalSem(UART_MUTEX);

        OS_Delay(50);  // 500ms between readings
    }
}

// ============= TASK 4 ? POT with Semaphores ================================
void TaskPotUart(void)
{
    for (;;) {
        // Protect ADC resource during conversion
        OSWaitSem(ADC_MUTEX, OSNO_TIMEOUT);
        uint16_t adc = ADC1_Read(POT_ADC_CH);
        OSSignalSem(ADC_MUTEX);
        
        float v = AdcToVoltage(adc);
        
        // Update shared sensor data structure
        g_SensorData.voltage = v;
        g_SensorData.pot_raw_adc = adc;

        char buf[80];
        sprintf(buf, "[POT]  %.2f V (ADC:%u) | Samples:%u\r\n", 
                (double)v, adc, g_SensorData.sample_count);
        
        // Protect UART resource during transmission
        OSWaitSem(UART_MUTEX, OSNO_TIMEOUT);
        UART2_WriteString(buf);
        
        // Print separator line every 10 samples
        if (g_SensorData.sample_count % 10 == 0) {
            UART2_WriteString("---------------------------\r\n");
        }
        OSSignalSem(UART_MUTEX);

        OS_Delay(50);  // 500ms between readings
    }
}

// ================= MAIN ==========================
int main(void)
{
    // Initialize hardware FIRST before creating tasks
    SYSTEM_Initialize();
    
    // Initialize Salvo RTOS
    OSInit();
    
    // ========== CREATE ALL SEMAPHORES ==========
    
    // SEMAPHORE 1: UART Mutex (Binary Semaphore)
    OSCreateSem(UART_MUTEX, 1);
    
    // SEMAPHORE 2: ADC Mutex (Binary Semaphore)
    OSCreateSem(ADC_MUTEX, 1);
    
    // SEMAPHORE 3: LED Mutex (Binary Semaphore)
    OSCreateSem(LED_MUTEX, 1);  
    
    // SEMAPHORE 4: Button Event (Binary Semaphore)
    OSCreateSem(BUTTON_EVENT_SEM, 0);

    // Create all tasks
    OSCreateTask(TaskKnightRider, TASK_KNIGHT_P, PRIO_KNIGHT);
    OSCreateTask(TaskRunLed,      TASK_RUNLED_P, PRIO_RUNLED);
    OSCreateTask(TaskTempUart,    TASK_TEMP_P,   PRIO_TEMP);
    OSCreateTask(TaskPotUart,     TASK_POT_P,    PRIO_POT);

    // Print startup message
    UART2_WriteString("\r\n=== RTOS Multi-Task System ===\r\n");
    UART2_WriteString("4 Semaphores Active:\r\n");
    UART2_WriteString("1. UART Mutex\r\n");
    UART2_WriteString("2. ADC Mutex\r\n");
    UART2_WriteString("3. LED Mutex\r\n");
    UART2_WriteString("4. Button Event Signal\r\n");
    UART2_WriteString("=============================\r\n\r\n");

    // Start RTOS scheduler
    for (;;)
        OSSched();
}

// =========== TIMER1 ISR (10ms RTOS tick) ===========================
void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void)
{
    static uint8_t tick_counter = 0;

    if (IFS0bits.T1IF) {
        IFS0bits.T1IF = 0;  // Clear interrupt flag
        
        tick_counter++;
        if (tick_counter >= 10) {
            tick_counter = 0;
            OSTimer();  // Call RTOS timer every 10ms
        }
    }
}

// =========== SYSTEM INITIALIZATION ================================
void SYSTEM_Initialize(void)
{
    OSCILLATOR_Initialize();
    PIN_MANAGER_Initialize();
    ADC1_Initialize();
    UART2_Initialize();
    TMR1_Initialize();  // Initialize timer last (starts interrupts)
}

void OSCILLATOR_Initialize(void)
{
    __builtin_write_OSCCONL((uint8_t)(0x0200 & 0x00FF));
    CLKDIV = 0x3100;
    OSCTUN = 0x0000;
    RCON   = 0x0000;
}

void PIN_MANAGER_Initialize(void)
{
    // Clear all LAT registers
    LATA = LATB = LATC = LATD = LATE = LATF = LATG = 0;

    // PORTA as output (LEDs), PORTB as input (button)
    TRISA = 0x0000;
    TRISB = 0xFFFF;

    // Configure analog inputs (AN4 and AN5 only)
    AD1PCFG = 0xFFFF;           // All digital by default
    AD1PCFGbits.PCFG4 = 0;      // AN4 as analog (temperature)
    AD1PCFGbits.PCFG5 = 0;      // AN5 as analog (potentiometer)
}

void TMR1_Initialize(void)
{
    TMR1 = 0;
    PR1  = 0x0FA0;  // Period = 4000 for 1ms interrupt @ 8MHz with 1:8 prescaler
    
    T1CON = 0;
    T1CONbits.TCKPS = 0b01;  // 1:8 prescaler
    
    // Clear and enable Timer1 interrupt
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;

    // Set Timer1 interrupt priority to 7 (high)
    IPC0bits.T1IP = 7;

    // Start Timer1
    T1CONbits.TON = 1;
}

void UART2_Initialize(void)
{
    U2MODE = 0x8000;  // UART enabled, 8-N-1
    U2STA  = 0x0000;
    U2BRG  = 0x0019;  // ~19200 baud @ 8MHz
    U2STAbits.UTXEN = 1;  // Enable transmitter
}

void ADC1_Initialize(void)
{
    AD1CON1 = 0x00E0;  // Auto-convert after sampling
    AD1CON2 = 0x0000;  // Vref+ = AVdd, Vref- = AVss
    AD1CON3 = 0x1F02;  // Sample time = 31 Tad, Tad = 3 Tcy
    AD1CHS  = 0x0000;  // Default channel
    AD1CON1bits.ADON = 1;  // Turn on ADC
}

uint16_t ADC1_Read(uint8_t ch)
{
    AD1CHS = ch;  // Select channel
    AD1CON1bits.SAMP = 1;  // Start sampling
    
    // Wait for acquisition time (~80?s)
    __delay32(_XTAL_FREQ / 100000);
    
    AD1CON1bits.SAMP = 0;  // Start conversion
    while (!AD1CON1bits.DONE);  // Wait for conversion complete
    
    return ADC1BUF0;
}

float AdcToVoltage(uint16_t v)
{
    return (3.3f * v) / 1023.0f;
}

float AdcToTempC(uint16_t v)
{
    float volts = AdcToVoltage(v);
    // TC1047A: 10mV/°C with 500mV offset at 0°C
    return (volts - 0.5f) * 100.0f;
}

void UART2_WriteChar(char c)
{
    while (U2STAbits.UTXBF);  // Wait if buffer full
    U2TXREG = c;
}

void UART2_WriteString(const char *s)
{
    while (*s)
        UART2_WriteChar(*s++);
}
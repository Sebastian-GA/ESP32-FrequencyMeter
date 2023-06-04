/*
 * Frequency Meter
 *
 * Authors: Sebastian Garcia Angarita
 *          Sergio Sebastian Oliveros Sepulveda
 *
 * This code is based on the work of the following original authors.
 * We made some modifications, fixed some bugs and adapted the code to our needs.
 * Credits: Rui Viana
 *          José Gustavo Abreu Murta
 *          https://github.com/Gustavomurta/ESP32_frequencyMeter
 *
 * Connections:
 *              - Input signal: FREQ_INPUT_PIN (GPIO 34)
 *              - Output PWM: PWM_OUTPUT_PIN (GPIO 33)
 *              - OUTPUT_CONTROL_GPIO (GPIO 32) <--> PCNT_INPUT_CTRL_IO (GPIO 35)
 */

#include "stdio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "soc/pcnt_struct.h"

#define LED_BUILTIN GPIO_NUM_2

#define PCNT_COUNT_UNIT PCNT_UNIT_0       // PCNT Unit 0
#define PCNT_COUNT_CHANNEL PCNT_CHANNEL_0 // PCNT Channel 0

#define PCNT_INPUT_SIG_IO GPIO_NUM_34 // Input pin for frequency meter
#define LEDC_HS_CH0_GPIO GPIO_NUM_33  // Output pin for LEDC PWM

#define PCNT_INPUT_CTRL_IO GPIO_NUM_35  // PCNT control pin - HIGH = count up, LOW = count down
#define OUTPUT_CONTROL_GPIO GPIO_NUM_32 // PCNT control pin (Connect to the PCNT_INPUT_CTRL_IO)
#define PCNT_H_LIM_VAL overflow         // Counter limit value

bool flag = true;                   // Flag to indicate the end of the measurement window
uint32_t overflow = 20000;          // Overflow value of the PCNT counter
int16_t pulses = 0;                 // Number of pulses counted in the measurement window
uint32_t multPulses = 0;            // Number of overflows of the PCNT counter
uint32_t window_interval = 1000000; // Measurement window of 1 second (measure pulses for 1 second)

uint32_t oscillator_freq = 15; // Frequency of the PWM signal generated by the ESP32
float frequency = 0;           // Measured frequency

// ESP-Timer setup
esp_timer_create_args_t create_args;
esp_timer_handle_t timer_handle;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED; // Mutex to protect the PCNT interrupt handler

// ----------------------------------- Oscillator ----------------------------------- //s
void setup_oscillator(uint32_t frequency)
{

    // See ESP32 Technical Reference Manual for more info
    // https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#ledpwm

    uint32_t resolution = (log(80000000 / frequency) / log(2)) / 2;
    if (resolution < 1)
        resolution = 1;
    // Serial.printf("PWM Resolution: %d\n", resolution);

    uint32_t duty = (pow(2, resolution)) / 2; // 50% duty cycle
    // Serial.printf("PWM Duty: %d\n", duty);

    // Timer configuration
    ledc_timer_config_t ledc_timer = {};

    ledc_timer.duty_resolution = ledc_timer_bit_t(resolution);
    ledc_timer.freq_hz = frequency;
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer_config(&ledc_timer);

    // LEDChannel configuration
    ledc_channel_config_t ledc_channel = {};

    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = duty;
    ledc_channel.gpio_num = LEDC_HS_CH0_GPIO; // Assign PWM output pin
    ledc_channel.intr_type = LEDC_INTR_DISABLE;
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.timer_sel = LEDC_TIMER_0; // Assign the Timer0 to LEDC channel 0
    ledc_channel_config(&ledc_channel);
}

// ----------------------------------- Pulse Counter ----------------------------------- //
static void IRAM_ATTR pcnt_intr_handler(void *arg) // PCNT Overflow Interrupt Handler
{
    portENTER_CRITICAL_ISR(&timerMux);       // Enter critical section - disable interrupts
    multPulses++;                            // Increment the number of overflows
    PCNT.int_clr.val = BIT(PCNT_COUNT_UNIT); // Clear the interrupt flag
    portEXIT_CRITICAL_ISR(&timerMux);        // Exit critical section - enable interrupts
}
void setup_counter(void)
{
    // Pulse Counter configuration
    pcnt_config_t pcnt_config = {};

    pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;
    pcnt_config.ctrl_gpio_num = PCNT_INPUT_CTRL_IO;
    pcnt_config.unit = PCNT_COUNT_UNIT;
    pcnt_config.channel = PCNT_COUNT_CHANNEL;
    pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;
    pcnt_config.pos_mode = PCNT_COUNT_INC;
    pcnt_config.neg_mode = PCNT_COUNT_INC;
    pcnt_config.lctrl_mode = PCNT_MODE_DISABLE;
    pcnt_config.hctrl_mode = PCNT_MODE_KEEP;
    pcnt_unit_config(&pcnt_config);

    pcnt_counter_pause(PCNT_COUNT_UNIT); // Pause the PCNT counter
    pcnt_counter_clear(PCNT_COUNT_UNIT); // Pause the PCNT counter

    // Enable the PCNT interrupt for Overflow
    pcnt_event_enable(PCNT_COUNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_isr_register(pcnt_intr_handler, NULL, 0, NULL);
    pcnt_intr_enable(PCNT_COUNT_UNIT);

    pcnt_counter_resume(PCNT_COUNT_UNIT); // Start the PCNT counter
}

// ----------------------------------- Frequency Meter ----------------------------------- //
void end_measurement(void *p)
{
    gpio_set_level(OUTPUT_CONTROL_GPIO, 0);           // Turn off the output control pin
    pcnt_get_counter_value(PCNT_COUNT_UNIT, &pulses); // Get the current counter value
    flag = true;
}
void setup_frequencyMeter()
{
    setup_oscillator(oscillator_freq);
    setup_counter();

    // Output control pin
    gpio_pad_select_gpio(OUTPUT_CONTROL_GPIO);
    gpio_set_direction(OUTPUT_CONTROL_GPIO, GPIO_MODE_OUTPUT);

    // ESP-Timer configuration
    create_args.callback = end_measurement; // Call function when the measurement window ends
    esp_timer_create(&create_args, &timer_handle);

    // Redirect the input frequency signal to the LED_BUILTIN
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
    gpio_matrix_in(PCNT_INPUT_SIG_IO, SIG_IN_FUNC226_IDX, false);
    gpio_matrix_out(LED_BUILTIN, SIG_IN_FUNC226_IDX, false, false);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("Frequency Meter [1Hz - 40MHz]");

    setup_frequencyMeter();
}

void loop()
{
    if (flag == true) // Se a contagem tiver terminado
    {
        flag = false;
        frequency = (pulses + (multPulses * overflow)) / 2;
        Serial.printf("Frequency: %f Hz\n", frequency);

        // Restart the measurement window
        multPulses = 0;
        delay(100);
        pcnt_counter_clear(PCNT_COUNT_UNIT);                 // Zera o contador PCNT
        esp_timer_start_once(timer_handle, window_interval); // Inicia contador de tempo de 1 segundo
        gpio_set_level(OUTPUT_CONTROL_GPIO, 1);              // Porta de controle - habilita contagem dos pulsos
    }

    // Update the oscillator frequency
    String inputString = "";
    oscillator_freq = 0;
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        inputString += inChar;
        if (inChar == '\n')
            oscillator_freq = inputString.toInt();
    }
    if (oscillator_freq != 0)
        setup_oscillator(oscillator_freq);
}
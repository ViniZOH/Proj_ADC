#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include "inc/font.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_J 22
#define LED_VERDE 11
#define LED_AZUL 12
#define LED_VERMELHO 13
#define JOYSTICK_X 26    
#define JOYSTICK_Y 27    
#define ZM 40

// **Ajuste do centro real do joystick**
#define CENTER_X 1939
#define CENTER_Y 2180

// **Tamanho do quadrado e tela SSD1306**
#define QUAD_SIZE 8  
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// **Posição inicial do quadrado (centro da tela)**
int posX = SCREEN_WIDTH / 2 - QUAD_SIZE / 2;
int posY = SCREEN_HEIGHT / 2 - QUAD_SIZE / 2;

// **Variáveis globais**
static volatile bool pwm_enabled = true;
static volatile uint32_t last_time_A = 0;
static volatile uint32_t last_time_J = 0;
static volatile bool border_thick = false; // Alterna a borda

// Prototipo das funções de interrupção
static void gpio_irq_handler(uint gpio, uint32_t events);

// **Inicialização do PWM**
void pwm_setup(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 4.0);
    pwm_set_wrap(slice, 65535);
    pwm_set_gpio_level(pin, 0);
    pwm_set_enabled(slice, true);
}

// **Função para definir o duty cycle do PWM**
void set_led_brightness(uint pin, uint16_t adc_value, int16_t center_value) {
    if (!pwm_enabled) {  
        pwm_set_gpio_level(pin, 0);
        return;
    }

    int16_t offset = adc_value - center_value;
    if (offset > -ZM && offset < ZM) {
        pwm_set_gpio_level(pin, 0);
        return;
    }

    uint16_t max_offset = (offset > 0) ? (4095 - center_value) : (center_value - 0);
    uint16_t pwm_value = ((abs(offset) - ZM) * 65535) / (max_offset - ZM);
    
    pwm_set_gpio_level(pin, pwm_value);
}

// **Função para ler um canal do ADC**
uint16_t read_adc(uint channel) {
    adc_select_input(channel);
    return adc_read();
}

// **Mapeia valores do ADC para a tela SSD1306**
int map_adc_to_screen(int adc_value, int center_value, int screen_max) {
    int range_min = center_value;     
    int range_max = 4095 - center_value;
    
    int offset = adc_value - center_value; 

    int mapped_value;
    if (offset < 0) {
        mapped_value = ((offset * (screen_max / 2)) / range_min) + (screen_max / 2);
    } else {
        mapped_value = ((offset * (screen_max / 2)) / range_max) + (screen_max / 2);
    }

    if (mapped_value < 0) mapped_value = 0;
    if (mapped_value > screen_max) mapped_value = screen_max;

    return mapped_value;
}

// **Atualiza a posição do quadrado no display**
void update_square_position() {
    uint16_t adc_x = read_adc(1);
    uint16_t adc_y = read_adc(0);

    int border_offset = border_thick ? 3 : 1;

    posX = map_adc_to_screen(adc_x, CENTER_X, SCREEN_WIDTH - QUAD_SIZE - border_offset);
    posY = SCREEN_HEIGHT - QUAD_SIZE - map_adc_to_screen(adc_y, CENTER_Y, SCREEN_HEIGHT - QUAD_SIZE - border_offset);

    if (posX < border_offset) posX = border_offset;
    if (posY < border_offset) posY = border_offset;
}

// **Desenha a borda no display**
void draw_border(ssd1306_t *ssd) {
    int thickness = border_thick ? 3 : 1;

    for (int i = 0; i < thickness; i++) {
        ssd1306_hline(ssd, 0, SCREEN_WIDTH - 1, i, true);
        ssd1306_hline(ssd, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1 - i, true);
        ssd1306_vline(ssd, i, 0, SCREEN_HEIGHT - 1, true);
        ssd1306_vline(ssd, SCREEN_WIDTH - 1 - i, 0, SCREEN_HEIGHT - 1, true);
    }
}

// **Atualiza o display**
void draw_display(ssd1306_t *ssd) {
    ssd1306_fill(ssd, false);
    draw_border(ssd);
    ssd1306_rect(ssd, posY, posX, QUAD_SIZE, QUAD_SIZE, true, true);
    ssd1306_send_data(ssd);
}

int main() {   
    stdio_init_all();

    pwm_setup(LED_VERMELHO);
    pwm_setup(LED_AZUL);

    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERDE, GPIO_OUT);
    gpio_put(LED_VERDE, 0); 

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A); 
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BOTAO_J);
    gpio_set_dir(BOTAO_J, GPIO_IN);
    gpio_pull_up(BOTAO_J);
    gpio_set_irq_enabled(BOTAO_J, GPIO_IRQ_EDGE_FALL, true);

    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);

    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_t ssd;
    ssd1306_init(&ssd, 128, 64, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    printf("Sistema Inicializado Corretamente\n");

    while (true) {
        update_square_position();
        draw_display(&ssd);

        set_led_brightness(LED_AZUL, read_adc(0), CENTER_X);
        set_led_brightness(LED_VERMELHO, read_adc(1), CENTER_Y);

        printf("X: %d | Y: %d | PWM: %s\n", read_adc(0), read_adc(1), pwm_enabled ? "ON" : "OFF");
        sleep_ms(50);
    }
}

// **Interrupção do Botão Joystick**
static void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t now = time_us_32();

    if (gpio == BOTAO_A) {
        if (now - last_time_A < 200000) return;
        last_time_A = now;
        
        pwm_enabled = !pwm_enabled;
        printf("Botão A pressionado! PWM %s\n", pwm_enabled ? "Ativado" : "Desativado");
    }

    if (gpio == BOTAO_J) {
        if (now - last_time_J < 200000) return;
        last_time_J = now;

        border_thick = !border_thick; 
        gpio_put(LED_VERDE, !gpio_get(LED_VERDE));  // Alterna o LED Verde
    }
}

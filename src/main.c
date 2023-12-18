#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
#include "bicicleta.h"
#include "bike_wheel.h"
#include "arm_math.h"




/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/

typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t seccond;
} calendar;

#define LV_HOR_RES_MAX          (240)
#define LV_VER_RES_MAX          (320)
#define TASK_SIMULATOR_STACK_SIZE (4096 / sizeof(portSTACK_TYPE))
#define TASK_SIMULATOR_STACK_PRIORITY (tskIDLE_PRIORITY)



LV_FONT_DECLARE(dseg70);


static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE      (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY  (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) { }
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

uint32_t current_hour, current_min, current_sec;
uint32_t current_year, current_month, current_day, current_week;


void return_button_handler(lv_event_t *e);
void arrow_up_handler(lv_event_t *e);
void arrow_down_handler(lv_event_t *e);
void RTC_Handler(void);
void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type);


int hora = 12;
int minuto = 47;
int segundo = 0;

#define RAIO 0.508/2
#define VEL_MAX_KMH  5.0f
#define VEL_MIN_KMH  0.5f
//#define RAMP
int ramp = 1;

static int wheel_radius = 20;
static int distancia_percurso = 0;
volatile int velocidade_percurso = 0;
static int duracao_percurso = 0;
static lv_obj_t * label_radius;
static int speed_principal = 0;

int acelerando = 0; //0 --> desacelerando, //1 --> acelerando, // 2 --> constante
static int captando_informacoes = 0;

SemaphoreHandle_t xSemaphore;
static lv_obj_t *label_big_number;
static lv_obj_t *icon;
static lv_obj_t *label_clock;




/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/
static lv_obj_t *scr1;  // screen 1 (lv_entrada)
static lv_obj_t *scr2;  // screen 2 (black page)
static lv_obj_t *scr3;  // screen 3 (settings page)

/**
* raio 20" => 50,8 cm (diametro) => 0.508/2 = 0.254m (raio)
* w = 2 pi f (m/s)
* v [km/h] = (w*r) / 3.6 = (2 pi f r) / 3.6
* f = v / (2 pi r 3.6)
* Exemplo : 5 km / h = 1.38 m/s
*           f = 0.87Hz
*           t = 1/f => 1/0.87 = 1,149s
*/
float kmh_to_hz(float vel, float raio) {
	float f = vel / (2*PI*raio*3.6);
	return(f);
}

void format_time(char *buffer, uint32_t hour, uint32_t min, uint32_t sec) {
	snprintf(buffer, 9, "%02d:%02d:%02d", hour, min, sec);
}

void lv_principal(void) {
	lv_obj_t *screen2 = lv_scr_act();
	static lv_obj_t *labelclock;

	labelclock = lv_label_create(lv_scr_act());
	lv_obj_align(labelclock, LV_ALIGN_TOP_RIGHT, 0, 30);
	lv_obj_set_style_text_color(labelclock, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelclock, "%02d:%02d", 17, 47);
}

static void event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		lv_scr_load(scr2);
		printf("Clicked\n");
		LV_LOG_USER("Clicked");
		} else if (code == LV_EVENT_VALUE_CHANGED) {
		LV_LOG_USER("Toggled");
	}
}

void lv_entrada(void) {
	lv_obj_t *label;
	lv_obj_t *screen = lv_scr_act();
	lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

	// Botao start
	lv_obj_t *btnstart = lv_btn_create(screen);
	lv_obj_add_event_cb(btnstart, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnstart, LV_ALIGN_CENTER, 0, 40);

	label = lv_label_create(btnstart);
	lv_label_set_text(label, "Start");
	lv_obj_center(label);

	// Foto da bike
	lv_obj_t *img = lv_img_create(lv_scr_act());
	lv_img_set_src(img, &bicicleta);
	lv_obj_align(img, LV_ALIGN_CENTER, 0, -20);
}

void refresh_button_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		printf("Refresh button clicked\n");
	}
}
void start_button_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		printf("Start button clicked\n");
	}
}

void pause_button_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		printf("Pause button clicked\n");
	}
}


void arrow_up_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {

		char *c;
		int raio;
		
		c = lv_label_get_text(label_radius);
		raio = atoi(c);
		lv_label_set_text_fmt(label_radius, "%d cm", raio + 1);
		wheel_radius++;
	}
}

void arrow_down_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {

		char *c;
		int raio;
		
		c = lv_label_get_text(label_radius);
		raio = atoi(c);
		lv_label_set_text_fmt(label_radius, "%d cm", raio - 1);
		wheel_radius--;
	}
}

void lv_settings_page(void) {
	lv_obj_clean(scr3);
	lv_obj_set_style_bg_color(scr3, lv_color_white(), LV_PART_MAIN);
	
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 5);


	// Create a return button
	lv_obj_t *btn_return = lv_btn_create(scr3);
	lv_obj_align(btn_return, LV_ALIGN_TOP_LEFT, 10, 10);
	lv_obj_set_size(btn_return, 60, 30);
	lv_obj_add_style(btn_return, &style, 0);
	lv_obj_add_event_cb(btn_return, return_button_handler, LV_EVENT_CLICKED, NULL);

	// Customize Return Button icon
	lv_obj_t *label_return = lv_label_create(btn_return);
	lv_label_set_text(label_return, LV_SYMBOL_LEFT);  // LV_SYMBOL_LEFT is the left arrow icon
	lv_obj_center(label_return);

	// Create an arrow-up button
	lv_obj_t *btn_up = lv_btn_create(scr3);
	lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 50, 80);
	lv_obj_set_size(btn_up, 60, 30);
	lv_obj_add_style(btn_up, &style, 0);
	lv_obj_add_event_cb(btn_up, arrow_up_handler, LV_EVENT_CLICKED, NULL);

	// Customize Arrow Up Button icon
	lv_obj_t *label_up = lv_label_create(btn_up);
	lv_label_set_text(label_up, LV_SYMBOL_PLUS);  // LV_SYMBOL_UP is the up arrow icon
	lv_obj_center(label_up);

	// Create an arrow-down button
	lv_obj_t *btn_down = lv_btn_create(scr3);
	lv_obj_align(btn_down, LV_ALIGN_TOP_MID, 50, 140);
	lv_obj_set_size(btn_down, 60, 30);
	lv_obj_add_style(btn_down, &style, 0);
	lv_obj_add_event_cb(btn_down, arrow_down_handler, LV_EVENT_CLICKED, NULL);

	// Customize Arrow Down Button icon
	lv_obj_t *label_down = lv_label_create(btn_down);
	lv_label_set_text(label_down, LV_SYMBOL_MINUS);  // LV_SYMBOL_DOWN is the down arrow icon
	lv_obj_center(label_down);
	
	lv_obj_t *img = lv_img_create(lv_scr_act());
	lv_img_set_src(img, &bike_wheel);
	lv_obj_align(img, LV_ALIGN_CENTER, -40, -20);
	
	label_radius = lv_label_create(scr3);
	lv_label_set_text_fmt(label_radius, "%d cm", wheel_radius);
	lv_obj_align(label_radius, LV_ALIGN_CENTER,-40, -80);
}

// Handler for the return button
void return_button_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		lv_scr_load(scr2);
		printf("Return button clicked\n");
	}
}



void settings_button_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		lv_scr_load(scr3);

		lv_settings_page();

		printf("Settings button clicked\n");
	}
}


void lv_black_page(void) {
	lv_obj_clean(scr2);
	lv_obj_set_style_bg_color(scr2, lv_color_white(), LV_PART_MAIN);

	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 5);

	lv_obj_t *btn_refresh = lv_btn_create(scr2);
	lv_obj_align(btn_refresh, LV_ALIGN_TOP_LEFT, 10, 10);
	lv_obj_set_size(btn_refresh, 30, 15);
	lv_obj_add_style(btn_refresh, &style, 0);
	lv_obj_add_event_cb(btn_refresh, refresh_button_handler, LV_EVENT_ALL, NULL);

	lv_obj_t *label_refresh = lv_label_create(btn_refresh);
	lv_label_set_text(label_refresh, LV_SYMBOL_REFRESH);
	lv_obj_center(label_refresh);

	// Add Start Button
	lv_obj_t *btn_start = lv_btn_create(scr2);
	lv_obj_align(btn_start, LV_ALIGN_TOP_LEFT, 50, 10);
	lv_obj_set_size(btn_start, 30, 15);
	lv_obj_add_style(btn_start, &style, 0);
	lv_obj_add_event_cb(btn_start, start_button_handler, LV_EVENT_ALL, NULL);

	// Customize Start Button icon
	lv_obj_t *label_start = lv_label_create(btn_start);
	lv_label_set_text(label_start, LV_SYMBOL_PLAY);  // LV_SYMBOL_PLAY is the play icon
	lv_obj_center(label_start);

	// Add Pause Button
	lv_obj_t *btn_pause = lv_btn_create(scr2);
	lv_obj_align(btn_pause, LV_ALIGN_TOP_LEFT, 90, 10);
	lv_obj_set_size(btn_pause, 30, 15);
	lv_obj_add_style(btn_pause, &style, 0);
	lv_obj_add_event_cb(btn_pause, pause_button_handler, LV_EVENT_ALL, NULL);

	// Customize Pause Button icon
	lv_obj_t *label_pause = lv_label_create(btn_pause);
	lv_label_set_text(label_pause, LV_SYMBOL_PAUSE);
	lv_obj_center(label_pause);
	
	// Add Settings Button (same style as Pause Button)
	lv_obj_t *btn_settings = lv_btn_create(scr2);
	lv_obj_align(btn_settings, LV_ALIGN_BOTTOM_LEFT, 10, -10);
	lv_obj_set_size(btn_settings, 60, 30);
	lv_obj_add_style(btn_settings, &style, 0);
	lv_obj_add_event_cb(btn_settings, settings_button_handler, LV_EVENT_ALL, NULL);

	// Customize Settings Button icon
	lv_obj_t *label_settings = lv_label_create(btn_settings);
	lv_label_set_text(label_settings, LV_SYMBOL_SETTINGS);
	lv_obj_center(label_settings);
	
	// Add Distance Label
	lv_obj_t *label_distance = lv_label_create(scr2);
	lv_label_set_text_fmt(label_distance, "Distancia: %d M", distancia_percurso);
	lv_obj_align(label_distance, LV_ALIGN_BOTTOM_MID, -40, -80);

	// Add Duration Label
	lv_obj_t *label_duration = lv_label_create(scr2);
	lv_label_set_text_fmt(label_duration, "Duracao: %d Min", duracao_percurso);
	lv_obj_align(label_duration, LV_ALIGN_BOTTOM_MID, -40, -60);

	// Add Speed Label
	lv_obj_t *label_speed = lv_label_create(scr2);
	lv_label_set_text_fmt(label_speed, "Vel media: %d KM/H", velocidade_percurso);
	lv_obj_align(label_speed, LV_ALIGN_BOTTOM_MID, -40, -100);
	
	label_big_number = lv_label_create(scr2);
	lv_label_set_text_fmt(label_big_number, "%d ", speed_principal);
	lv_obj_set_style_text_font(label_big_number, &dseg70, 0);
	lv_obj_align(label_big_number, LV_ALIGN_CENTER, -20, -20);
	
	printf("Acelerando: %d",acelerando);
	
	icon = lv_label_create(scr2);
	lv_label_set_text(icon, LV_SYMBOL_MINUS);
	lv_obj_align(icon, LV_ALIGN_CENTER, 40, 0);

	


	
	
	lv_obj_t *captacao_icon;
	if (captando_informacoes == 1) {
		captacao_icon = lv_label_create(scr2);
		lv_label_set_text(captacao_icon, LV_SYMBOL_OK);
		} else {
		captacao_icon = lv_label_create(scr2);
		lv_label_set_text(captacao_icon, LV_SYMBOL_CLOSE);
	}
	
	lv_obj_align(captacao_icon, LV_ALIGN_CENTER, 40, -20);
	
	label_clock = lv_label_create(scr2);
	lv_obj_align(label_clock, LV_ALIGN_TOP_RIGHT, -10, 10);
	lv_label_set_text_fmt(label_clock, "%02d:%02d:%02d", hora, minuto, segundo);


}



/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_entrada(void *pvParameters) {
	lv_entrada();

	for (;;) {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

static void task_black_page(void *pvParameters) {
	lv_black_page();

	for (;;) {


		vTaskDelay(1000);
	}
}

static void task_simulador(void *pvParameters) {

	pmc_enable_periph_clk(ID_PIOC);
	pio_set_output(PIOC, PIO_PC31, 1, 0, 0);

	float vel = VEL_MAX_KMH;
	float f;
	int ramp_up = 1;
	int vel_passada = 0;

	while(1){
		pio_clear(PIOC, PIO_PC31);
		delay_ms(1);
		pio_set(PIOC, PIO_PC31);
		if (ramp){
			
			if (ramp_up) {
				printf("[SIMU] ACELERANDO: %d \n", (int) (10*vel));
				acelerando = 1;
				
				lv_label_set_text(icon, LV_SYMBOL_UP);
				
				
				
				
				vel += 0.5;
				} else {
				printf("[SIMU] DESACELERANDO: %d \n",  (int) (10*vel));
				lv_label_set_text(icon, LV_SYMBOL_DOWN);

				acelerando = 0;
				vel -= 0.5;
			}

			if (vel >= VEL_MAX_KMH)
			ramp_up = 0;
			else if (vel <= VEL_MIN_KMH)
			ramp_up = 1;
			}else{
			vel = 5;
			printf("[SIMU] CONSTANTE: %d \n", (int) (10*vel));
			lv_label_set_text(icon, LV_SYMBOL_MINUS);

			acelerando = 2;
		}
						lv_label_set_text_fmt(label_clock, "%02d:%02d:%02d", hora, minuto, segundo+=1);
						if (segundo>= 59){
							segundo = 0;
							minuto++;
						}
						if (minuto>=60){
							minuto = 0;
							segundo = 0;
							hora++;
						}
						if (hora>=24){
							hora = 0;
							minuto = 0;
							segundo = 0;
						}
		lv_label_set_text_fmt(label_big_number, "%d ", (int)(10 * vel));


		
		

		f = kmh_to_hz(vel, (RAIO));
		int t = 965*(1.0/f); //UTILIZADO 965 como multiplicador ao invés de 1000
		//para compensar o atraso gerado pelo Escalonador do freeRTOS
		


		delay_ms(t);
		
	}
}
/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	/**LCD pin configure on SPI*/
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	
	ili9341_init();
	ili9341_backlight_on();
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
		.charlength = USART_SERIAL_CHAR_LENGTH,
		.paritytype = USART_SERIAL_PARITY,
		.stopbits = USART_SERIAL_STOP_BIT,
	};

	/* Configure console UART. */
	stdio_serial_init(CONSOLE_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	
	/* IMPORTANT!!!
	* Inform the graphics library that you are ready with the flushing*/
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	
	if (readPoint(&px, &py))
	data->state = LV_INDEV_STATE_PRESSED;
	else
	data->state = LV_INDEV_STATE_RELEASED;
	
	data->point.x = py;
	data->point.y = 320-px;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);

	lv_disp_drv_init(&disp_drv);
	disp_drv.draw_buf = &disp_buf;
	disp_drv.flush_cb = my_flush_cb;
	disp_drv.hor_res = LV_HOR_RES_MAX;
	disp_drv.ver_res = LV_VER_RES_MAX;

	lv_disp_drv_register(&disp_drv);

	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_drv_register(&indev_drv);
}

/************************************************************************/
/* Main                                                                 */
/************************************************************************/

int main(void) {
	board_init();
	sysclk_init();
	configure_console();
	configure_lcd();
	configure_touch();
	configure_lvgl();
	ili9341_set_orientation(ILI9341_FLIP_Y | ILI9341_SWITCH_XY);
	

	// Initialize screens
	scr1 = lv_obj_create(NULL);
	scr2 = lv_obj_create(NULL);
	scr3 = lv_obj_create(NULL);
	
	xSemaphore = xSemaphoreCreateBinary();
	if (xSemaphore == NULL)
	printf("falha em criar o semaforo \n");


	// Create tasks
	if (xTaskCreate(task_entrada, "Entrada", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create entrada task\r\n");
	}

	if (xTaskCreate(task_black_page, "BlackPage", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create black page task\r\n");
	}
	if (xTaskCreate(task_simulador, "SIMUL", TASK_SIMULATOR_STACK_SIZE, NULL, TASK_SIMULATOR_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	

	vTaskStartScheduler();

	while (1) {
	}
}
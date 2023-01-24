

#include <stdbool.h>
#include <stdint.h>
#include "nordic_common.h"
#include "bsp.h"
#include "nrf_soc.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "app_timer.h"
#include "nrf_pwr_mgmt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define TWI_INSTANCE_ID     0
//#define NRFX_TWIM_ENABLED 1
#define TWI_ENABLED 1
#include <stdio.h>
#include "app_util_platform.h"
#include "nrf_drv_twi.h"
#include "nrf_delay.h"
#include "bme280_twi.h"

#define APP_BLE_CONN_CFG_TAG            1                                  /**< A tag identifying the SoftDevice BLE configuration. */

#define NON_CONNECTABLE_ADV_INTERVAL    MSEC_TO_UNITS(100, UNIT_0_625_MS)  /**< The advertising interval for non-connectable advertisement (100 ms). This value can vary between 100ms to 10.24s). */
#define APP_ADV_DURATION                3000

#define APP_BEACON_INFO_LENGTH          0x17                               /**< Total length of information advertised by the Beacon. */
#define APP_ADV_DATA_LENGTH             0x15                               /**< Length of manufacturer specific data in the advertisement. */
#define APP_DEVICE_TYPE                 0x02                               /**< 0x02 refers to Beacon. */
#define APP_MEASURED_RSSI               0xC3                               /**< The Beacon's measured RSSI at 1 meter distance in dBm. */
#define APP_COMPANY_IDENTIFIER          0x0005          // 0005 3Com nostalgy, 0x0059                             /**< Company identifier for Nordic Semiconductor ASA. as per www.bluetooth.org. */
#define APP_MAJOR_VALUE                 0x01, 0x02                         /**< Major value used to identify Beacons. */
#define APP_MINOR_VALUE                 0x03, 0x04                         /**< Minor value used to identify Beacons. */
#define APP_BEACON_UUID                 0x01, 0x12, 0x23, 0x34, \
                                        0x45, 0x56, 0x67, 0x78, \
                                        0x89, 0x9a, 0xab, 0xbc, \
                                        0xcd, 0xde, 0xef, 0xf0            /**< Proprietary UUID for Beacon. */

#define DEAD_BEEF                       0xDEADBEEF                         /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#if defined(USE_UICR_FOR_MAJ_MIN_VALUES)
#define MAJ_VAL_OFFSET_IN_BEACON_INFO   18                                 /**< Position of the MSB of the Major Value in m_beacon_info array. */
#define UICR_ADDRESS                    0x10001080                         /**< Address of the UICR register used by this example. The major and minor versions to be encoded into the advertising data will be picked up from this location. */
#endif

static ble_gap_adv_params_t m_adv_params;                                  /**< Parameters to be passed to the stack when starting advertising. */
static uint8_t              m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET; /**< Advertising handle used to identify an advertising set. */
static uint8_t              m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];  /**< Buffer for storing an encoded advertising set. */

/**@brief Struct that contains pointers to the encoded advertising data. */
static ble_gap_adv_data_t m_adv_data =
{
    .adv_data =
    {
        .p_data = m_enc_advdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    },
    .scan_rsp_data =
    {
        .p_data = NULL,
        .len    = 0

    }
};


static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(0);




void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Function for initializing the Advertising functionality.
 *
 * @details Encodes the required advertising data and passes it to the stack.
 *          Also builds a structure to be passed to the stack when starting advertising.
 */
static void advertising_init(void)
{
    uint32_t      err_code;
    ble_advdata_t advdata;
    uint8_t       flags = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

    ble_advdata_manuf_data_t manuf_specific_data;

    manuf_specific_data.company_identifier = APP_COMPANY_IDENTIFIER;


    uint8_t data[]     = " 123456789"; //Our data to advertise

    manuf_specific_data.data.p_data                    = data;
    manuf_specific_data.data.size                      = sizeof(data);

    // Build and set advertising data.
    memset(&advdata, 0, sizeof(advdata));

    advdata.name_type             = BLE_ADVDATA_NO_NAME;
    advdata.flags                 = flags;
    advdata.p_manuf_specific_data = &manuf_specific_data;

    // Initialize advertising parameters (used when starting advertising).
    memset(&m_adv_params, 0, sizeof(m_adv_params));

    m_adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    m_adv_params.p_peer_addr     = NULL;    // Undirected advertisement.
    m_adv_params.filter_policy   = BLE_GAP_ADV_FP_ANY;
    m_adv_params.interval        = NON_CONNECTABLE_ADV_INTERVAL;
    m_adv_params.duration        = APP_ADV_DURATION;

    err_code = ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &m_adv_params);
    APP_ERROR_CHECK(err_code);

}


/**@brief Function for starting advertising.
 */
static void advertising_start(void)
{
    ret_code_t err_code;

    err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing logging. */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing LEDs. */
static void leds_init(void)
{
    ret_code_t err_code = bsp_init(BSP_INIT_LEDS, NULL);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing power management.
 */
static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling the idle state (main loop).

 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 */
static void idle_state_handle(void)
{
    if (NRF_LOG_PROCESS() == false)
    {
        nrf_pwr_mgmt_run();
    }
}



/* ================= END OF SDK bullshit, start of my code ===================*/
static uint8_t counter=0; //count timer rounds

void bme280_handler(bme280_twi_evt_t const * p_event, void * p_context)
{
	switch (p_event->type) {
		case BME280_TWI_MEASUREMENT_FETCHED:
			//m_measurement_fetched = true;
			break;
		default:
			break;
	}
}

void twi_init(void)
{
	const nrf_drv_twi_config_t twi_config = {
		.scl                = 13,
		.sda                = 14,
		.frequency          = NRF_TWI_FREQ_100K,
		.interrupt_priority = APP_IRQ_PRIORITY_HIGH,
		.clear_bus_init     = false
	};

	ret_code_t err_code = nrf_drv_twi_init(&m_twi, &twi_config, bme280_twi_evt_handler, NULL);
	APP_ERROR_CHECK(err_code);

	nrf_drv_twi_enable(&m_twi);
}

void bme280_init(void) {
	const bme280_twi_config_t bme280_twi_config = {
		.addr = BME280_TWI_ADDR_0,
		.standby = BME280_TWI_STANDBY_250_MS,
		.filter = BME280_TWI_FILTER_OFF,
		.temp_oversampling = BME280_TWI_OVERSAMPLING_X1,
		.pres_oversampling = BME280_TWI_OVERSAMPLING_X1
	};

	bme280_twi_init(&m_twi, &bme280_twi_config, bme280_handler, NULL);
	
}

bme280_twi_data_t bme280_get_measurements(){
	bme280_twi_enable();
	bme280_twi_measurement_fetch();
	nrf_delay_ms(50);
	bme280_twi_data_t data;
	bme280_twi_measurement_get(&data);
	return data; //((float)data.temp)/100;
}

static void update_adv_values(void * p_context)
{

    uint32_t      err_code;
    ble_advdata_t advdata;
    uint8_t       flags = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;
    UNUSED_PARAMETER(p_context);
    ble_advdata_manuf_data_t manuf_specific_data;
    manuf_specific_data.company_identifier = APP_COMPANY_IDENTIFIER;

    uint8_t data[9]  ; //Our data to advertise
    data[0]=counter++;
    bme280_twi_data_t measured_data;
    measured_data = bme280_get_measurements();
    
    //int32_t temp;
    //err_code = sd_temp_get(&temp);
    //temp = bme280_gettemp();
    //APP_ERROR_CHECK(err_code);    
    
    //temp returns 16 bit value (last 2 digits are begid decimal dot) 
    int16_t temp_value;
    
    temp_value= (int16_t)measured_data.temp;
	data[1]=(temp_value>>8)& 0xFF;
	data[2]=temp_value & 0xFF;
    //press returns 16 bit value (in hPa)
    //temp_value = (int16_t)((measured_data.pres/256)/100);
    //data[3]=(temp_value>>8)& 0xFF;
	//data[4]=temp_value & 0xFF;
	data[3] = (int8_t)((measured_data.pres>>24) & 0xFF);
	data[4] = (int8_t)((measured_data.pres>>16) & 0xFF);
	data[5] = (int8_t)((measured_data.pres>>8) & 0xFF);
	data[6] = (int8_t)(measured_data.pres & 0xFF);
    
    //data[1]=(int8_t)temp/4;

    manuf_specific_data.data.p_data  = data;
    manuf_specific_data.data.size   = sizeof(data);

    // Build and set advertising data.
    memset(&advdata, 0, sizeof(advdata));

    advdata.name_type             = BLE_ADVDATA_NO_NAME;
    advdata.flags                 = flags;
    advdata.p_manuf_specific_data = &manuf_specific_data;


    err_code = ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &m_adv_params);
    APP_ERROR_CHECK(err_code);

    advertising_start();
}

APP_TIMER_DEF(m_timer_id); //timer for sleep/adverisment

static void application_timers_start(void)
{

       ret_code_t err_code;
       err_code = app_timer_start(m_timer_id, APP_TIMER_TICKS(100000), NULL); //check this for timer period: https://devzone.nordicsemi.com/f/nordic-q-a/52080/ticks-app_timer_ticks-ms
       APP_ERROR_CHECK(err_code);

}


/**@brief Function for initializing timers. */
static void timers_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Create timers.
    err_code = app_timer_create(&m_timer_id,
                              APP_TIMER_MODE_REPEATED,
                              update_adv_values);
    APP_ERROR_CHECK(err_code);
}






/**
 * @brief Function for application main entry.
 */
int main(void)
{
    // Initialize.
    log_init();
    timers_init();
    leds_init();
    power_management_init();
    ble_stack_init();
    advertising_init();
    twi_init();
    bme280_init();

    // Start execution.
    NRF_LOG_INFO("Beacon example started.");
    advertising_start();
    application_timers_start();
    // Enter main loop.
    for (;; )
    {
        idle_state_handle();
    }
}


/**
 * @}
 */

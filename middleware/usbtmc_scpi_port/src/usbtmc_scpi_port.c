#include "usbtmc_scpi_port.h"

#include <stdint.h>
#include <string.h>

#include "class/usbtmc/usbtmc.h"
#include "class/usbtmc/usbtmc_device.h"
#include "pico/unique_id.h"
#include "project_config.h"
#include "scpi_port.h"
#include "tusb.h"

#define USBTMC_SCPI_COMMAND_BUFFER_LENGTH 768u
#define USBTMC_SCPI_RESPONSE_BUFFER_LENGTH 2048u
#define USBTMC_SCPI_USB_BCD 0x0200u
#define USBTMC_SCPI_DEVICE_BCD 0x0100u
#define USBTMC_SCPI_BULK_EP_OUT 0x01u
#define USBTMC_SCPI_BULK_EP_IN 0x81u
#define USBTMC_SCPI_INT_EP_IN 0x82u
#define USBTMC_SCPI_INT_EP_SIZE 8u
#define USBTMC_SCPI_INT_EP_INTERVAL 16u

#define IEEE4882_STB_MAV 0x10u

static uint8_t s_command_buffer[USBTMC_SCPI_COMMAND_BUFFER_LENGTH];
static size_t s_command_len;
static char s_response_buffer[USBTMC_SCPI_RESPONSE_BUFFER_LENGTH];
static size_t s_response_len;
static size_t s_response_offset;
static bool s_response_ready;
static bool s_bulk_in_requested;
static uint8_t s_status_byte;

static const usbtmc_response_capabilities_488_t s_usbtmc_capabilities = {
    .USBTMC_status = USBTMC_STATUS_SUCCESS,
    .bcdUSBTMC = USBTMC_VERSION,
    .bmIntfcCapabilities = {
        .listenOnly = 0,
        .talkOnly = 0,
        .supportsIndicatorPulse = 1,
    },
    .bmDevCapabilities = {
        .canEndBulkInOnTermChar = 0,
    },
    .bcdUSB488 = USBTMC_488_VERSION,
    .bmIntfcCapabilities488 = {
        .supportsTrigger = 1,
        .supportsREN_GTL_LLO = 0,
        .is488_2 = 1,
    },
    .bmDevCapabilities488 = {
        .SCPI = 1,
        .SR1 = 0,
        .RL1 = 0,
        .DT1 = 0,
    },
};

bool usbtmc_scpi_port_init(void)
{
    tusb_init();
    return true;
}

void usbtmc_scpi_port_service(void)
{
    tud_task();
}

void tud_usbtmc_open_cb(uint8_t interface_id)
{
    (void)interface_id;
    tud_usbtmc_start_bus_read();
}

usbtmc_response_capabilities_488_t const *tud_usbtmc_get_capabilities_cb(void)
{
    return &s_usbtmc_capabilities;
}

bool tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t *msg)
{
    (void)msg;
    return true;
}

bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const *msg_header)
{
    s_command_len = 0u;
    if (msg_header->TransferSize >= sizeof(s_command_buffer)) {
        return false;
    }
    return true;
}

bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete)
{
    if (len > sizeof(s_command_buffer) - s_command_len) {
        return false;
    }

    memcpy(&s_command_buffer[s_command_len], data, len);
    s_command_len += len;

    if (transfer_complete) {
        if (s_command_len == 0u ||
            (s_command_buffer[s_command_len - 1u] != '\n' &&
             s_command_buffer[s_command_len - 1u] != '\r')) {
            if (s_command_len >= sizeof(s_command_buffer)) {
                return false;
            }
            s_command_buffer[s_command_len] = '\n';
            s_command_len++;
        }

        s_response_len = 0u;
        s_response_offset = 0u;
        s_response_ready = false;
        s_bulk_in_requested = false;
        s_status_byte &= (uint8_t)~IEEE4882_STB_MAV;

        if (!scpi_port_execute((const char *)s_command_buffer,
                               s_command_len,
                               s_response_buffer,
                               sizeof(s_response_buffer),
                               &s_response_len)) {
            return false;
        }

        if (s_response_len > 0u) {
            s_response_ready = true;
            s_status_byte |= IEEE4882_STB_MAV;
        }

        s_command_len = 0u;
    }

    tud_usbtmc_start_bus_read();
    return true;
}

bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const *request)
{
    s_bulk_in_requested = true;

    if (s_response_ready && s_response_offset < s_response_len) {
        const size_t remaining = s_response_len - s_response_offset;
        const size_t requested = request->TransferSize;
        const size_t tx_len = remaining < requested ? remaining : requested;
        const bool complete = tx_len >= remaining;

        if (tx_len > 0u) {
            tud_usbtmc_transmit_dev_msg_data(&s_response_buffer[s_response_offset],
                                             tx_len,
                                             complete,
                                             false);
            s_response_offset += tx_len;
        }
    }

    return true;
}

bool tud_usbtmc_msgBulkIn_complete_cb(void)
{
    if (s_response_offset >= s_response_len) {
        s_response_len = 0u;
        s_response_offset = 0u;
        s_response_ready = false;
        s_bulk_in_requested = false;
        s_status_byte &= (uint8_t)~IEEE4882_STB_MAV;
    }

    tud_usbtmc_start_bus_read();
    return true;
}

bool tud_usbtmc_initiate_clear_cb(uint8_t *tmc_result)
{
    *tmc_result = USBTMC_STATUS_SUCCESS;
    s_command_len = 0u;
    s_response_len = 0u;
    s_response_offset = 0u;
    s_response_ready = false;
    s_bulk_in_requested = false;
    s_status_byte = 0u;
    return true;
}

bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp)
{
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->bmClear.BulkInFifoBytes = 0u;
    tud_usbtmc_start_bus_read();
    return true;
}

bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmc_result)
{
    *tmc_result = USBTMC_STATUS_SUCCESS;
    s_response_len = 0u;
    s_response_offset = 0u;
    s_response_ready = false;
    s_bulk_in_requested = false;
    s_status_byte &= (uint8_t)~IEEE4882_STB_MAV;
    return true;
}

bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->bmAbortBulkIn.BulkInFifoBytes = 0u;
    rsp->NBYTES_RXD_TXD = 0u;
    tud_usbtmc_start_bus_read();
    return true;
}

bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmc_result)
{
    *tmc_result = USBTMC_STATUS_SUCCESS;
    s_command_len = 0u;
    return true;
}

bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->bmAbortBulkIn.BulkInFifoBytes = 0u;
    rsp->NBYTES_RXD_TXD = 0u;
    tud_usbtmc_start_bus_read();
    return true;
}

void tud_usbtmc_bulkIn_clearFeature_cb(void)
{
    s_response_len = 0u;
    s_response_offset = 0u;
    s_response_ready = false;
    s_bulk_in_requested = false;
    s_status_byte &= (uint8_t)~IEEE4882_STB_MAV;
}

void tud_usbtmc_bulkOut_clearFeature_cb(void)
{
    s_command_len = 0u;
    tud_usbtmc_start_bus_read();
}

uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmc_result)
{
    *tmc_result = USBTMC_STATUS_SUCCESS;
    return s_status_byte;
}

bool tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const *msg, uint8_t *tmc_result)
{
    (void)msg;
    *tmc_result = USBTMC_STATUS_SUCCESS;
    return true;
}

static tusb_desc_device_t const s_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USBTMC_SCPI_USB_BCD,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = PROJECT_USB_VID,
    .idProduct = PROJECT_USB_PID_USBTMC,
    .bcdDevice = USBTMC_SCPI_DEVICE_BCD,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_desc_device;
}

#define TUD_USBTMC_DESC_MAIN(_itfnum, _bNumEndpoints, _bulkMaxPacketLength) \
    TUD_USBTMC_IF_DESCRIPTOR(_itfnum, _bNumEndpoints, 4u, TUD_USBTMC_PROTOCOL_USB488), \
    TUD_USBTMC_BULK_DESCRIPTORS(USBTMC_SCPI_BULK_EP_OUT, USBTMC_SCPI_BULK_EP_IN, _bulkMaxPacketLength)

#define TUD_USBTMC_DESC(_itfnum, _bulkMaxPacketLength) \
    TUD_USBTMC_DESC_MAIN(_itfnum, 3u, _bulkMaxPacketLength), \
    TUD_USBTMC_INT_DESCRIPTOR(USBTMC_SCPI_INT_EP_IN, \
                              USBTMC_SCPI_INT_EP_SIZE, \
                              USBTMC_SCPI_INT_EP_INTERVAL)

#define TUD_USBTMC_DESC_LEN \
    (TUD_USBTMC_IF_DESCRIPTOR_LEN + TUD_USBTMC_BULK_DESCRIPTORS_LEN + TUD_USBTMC_INT_DESCRIPTOR_LEN)

enum {
    ITF_NUM_USBTMC,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_USBTMC_DESC_LEN)

static uint8_t const s_desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_USBTMC_DESC(ITF_NUM_USBTMC, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_fs_configuration;
}

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_USBTMC_INTERFACE,
};

static char const *const s_string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    PROJECT_USB_MANUFACTURER,
    PROJECT_USB_PRODUCT_USBTMC,
    NULL,
    "USBTMC USB488 SCPI",
};

static uint16_t s_desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&s_desc_str[1], s_string_desc_arr[0], 2);
        chr_count = 1u;
    } else {
        char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2u + 1u];
        const char *str;

        if (index == STRID_SERIAL) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            str = serial;
        } else {
            if (index >= sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0])) {
                return NULL;
            }
            str = s_string_desc_arr[index];
        }

        chr_count = strlen(str);
        const size_t max_count = sizeof(s_desc_str) / sizeof(s_desc_str[0]) - 1u;
        if (chr_count > max_count) {
            chr_count = max_count;
        }

        for (size_t i = 0u; i < chr_count; i++) {
            s_desc_str[1u + i] = (uint8_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (2u * chr_count + 2u));
    return s_desc_str;
}

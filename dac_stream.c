/*
 *  * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_mem.h"
#include "audio_sys.h"
#include "audio_error.h"
#include "audio_element.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/dac_continuous.h"
#include "audio_idf_version.h"
#include "dac_stream.h"

static const char *TAG = "DAC_STREAM";

#define BUFFER_MIN_SIZE (256UL)
#define SAMPLE_RATE_MAX (48000)
#define SAMPLE_RATE_MIN (648)
#define CHANNEL_LEFT_INDEX  (0)
#define CHANNEL_RIGHT_INDEX (1)
#define CHANNEL_LEFT_MASK   (0x01)
#define CHANNEL_RIGHT_MASK  (0x02)
#define AUDIO_DAC_CH_MAX (2)

typedef struct dac_stream {
    dac_stream_cfg_t        config;
    bool                    is_open;
    bool                    reinit;
    dac_continuous_handle_t dac_handle;
    uint8_t                 *data;
    uint32_t                channel_set_num;                 /**< channel audio set number */
    int32_t                 framerate;                       /*!< frame rates in Hz */
    int32_t                 bits_per_sample;                 /*!< bits per sample (16, 32) */
} dac_stream_t;

//

static esp_err_t audio_dac_set_param(dac_stream_t *handle, int rate, int bits, int ch)
{
    esp_err_t res = ESP_OK;
    if (rate > SAMPLE_RATE_MAX || rate < SAMPLE_RATE_MIN) {
        ESP_LOGE(TAG, "%s:%d (%s): AUDIO DAC SAMPLE IS %d AND SHOULD BE BETWEEN 648 AND 48000", __FILENAME__, __LINE__, __FUNCTION__, rate);
    }
    if (!(bits == 32 || bits == 16 || bits == 8)) {
        ESP_LOGE(TAG, "%s:%d (%s): AUDIO DAC BITS IS %d AND SHOULD BE 8, 16, 32", __FILENAME__, __LINE__, __FUNCTION__, bits);
    }
    if (!(ch == 2 || ch == 1)) {
        ESP_LOGE(TAG, "%s:%d (%s): AUDIO DAC CH IS %d AND SHOULD BE 1 OR 2", __FILENAME__, __LINE__, __FUNCTION__, ch);
    }
    ESP_LOGI(TAG, "Set audio DAC param, rate %d, bits %d, ch %d", rate, bits, ch);
    if (handle->framerate != rate || handle->bits_per_sample != bits || handle->channel_set_num != ch) {
        handle->reinit = handle->is_open;
    }
    handle->framerate = rate;
    handle->bits_per_sample = bits;
    handle->channel_set_num = ch;
    return res;
}

static esp_err_t dac_data_convert(uint8_t *outbuf, size_t *outbuf_len, uint8_t *inbuf, size_t inbuf_len, size_t *bytes_written, int32_t bits_per_sample, int32_t channels, dac_output_type_t output_type)
{
    int8_t* inbuf8 = (int8_t*)inbuf;
    int16_t* inbuf16 = (int16_t*)inbuf;
    int32_t* inbuf32 = (int32_t*)inbuf;
    int inlen8 = inbuf_len;
    int inlen16 = inbuf_len / 2;
    int inlen32 = inbuf_len / 4;

    //ESP_LOGI(TAG, "Convert data, inbuf_len %d, outbuf_len %d, bits_per_sample %d, channels %d, output_type %d", inbuf_len, *outbuf_len, bits_per_sample, channels, output_type);
    int o = 0, i = 0;
    if (channels == 1 && output_type == DAC_OUTPUT_TYPE_STEREO) {
        // Copy mono data to both channels
        //ESP_LOGI(TAG, "Copy mono data to both channels");
        if (bits_per_sample == 32) {
            for (; i < inlen32; i++, o += 2) {
                outbuf[o] = outbuf[o+1] = inbuf32[i] / (256*256*256);
            }
        } else if (bits_per_sample == 16) {
            for (; i < inlen16 && o < 2047; i++, o += 2) {
                outbuf[o] = outbuf[o+1] = inbuf16[i] / 256;
            }
        } else { // 8 bits
            // The only case where output is bigger than input
            for (; i < inlen8 && o < *outbuf_len && o < 2047; i++, o += 2) {
                outbuf[o] = outbuf[o+1] = inbuf8[i];
            }
        }
    } else if (channels == 1 || (channels == 2 && output_type == DAC_OUTPUT_TYPE_STEREO)) {
        // Copy data without changes
        //ESP_LOGI(TAG, "Copy data without changes");
        if (bits_per_sample == 32) {
            for (; i < inlen32; i++, o++) {
                outbuf[o] = inbuf32[i] / (256*256*256);
            }
        } else if (bits_per_sample == 16) {
            for (; i < inlen16; i++, o++) {
                outbuf[o] = inbuf16[i] / 256;
            }
        } else { // 8 bits
            for (; i < inlen8; i++, o++) {
                outbuf[o] = inbuf8[i];
            }
        }
    // 2 channels with the rest of the channel types
    } else if (output_type == DAC_OUTPUT_TYPE_MONO_MIX) {
        // Average left and right channel data to both channels
        //ESP_LOGI(TAG, "Average left and right channel data to both channels");
        if (bits_per_sample == 32) {
            for (; i < inlen32; i += 2, o++) {
                outbuf[o] = (inbuf32[i]/2 + inbuf32[i+1]/2) / (256*256*256); // prevent int32 overflow
            }
        } else if (bits_per_sample == 16) {
            for (; i < inlen16; i += 2, o++) {
                outbuf[o] = (inbuf16[i]/2 + inbuf16[i+1]/2) / 256;
            }
        } else { // 8 bits
            for (; i < inlen8; i += 2, o++) {
                outbuf[o] = inbuf8[i]/2 + inbuf8[i+1]/2;
            }
        }
    } else if (output_type == DAC_OUTPUT_TYPE_MONO_LEFT) {
        // Copy only left channel data to both channels
        if (bits_per_sample == 32) {
            for (; i < inlen32; i += 2, o++) {
                outbuf[o] = inbuf32[i+CHANNEL_LEFT_INDEX] / (256*256*256);
            }
        } else if (bits_per_sample == 16) {
            for (; i < inlen16; i += 2, o++) {
                outbuf[o] = inbuf16[i+CHANNEL_LEFT_INDEX] / 256;
            }
        } else { // 8 bits
            for (; i < inlen8; i += 2, o++) {
                outbuf[o] = inbuf8[i+CHANNEL_LEFT_INDEX];
            }
        }
    } else if (output_type == DAC_OUTPUT_TYPE_MONO_RIGHT) {
        // Copy only right channel data to both channels
        if (bits_per_sample == 32) {
            for (; i < inlen32; i += 2, o++) {
                outbuf[o] = inbuf32[i+CHANNEL_RIGHT_INDEX] / (256*256*256);
            }
        } else if (bits_per_sample == 16) {
            for (; i < inlen16; i += 2, o++) {
                outbuf[o] = inbuf16[i+CHANNEL_RIGHT_INDEX] / 256;
            }
        } else { // 8 bits
            for (; i < inlen8; i += 2, o++) {
                outbuf[o] = inbuf8[i+CHANNEL_RIGHT_INDEX];
            }
        }
    } else {
        ESP_LOGE(TAG, "Unsupported channel type %d, channels %d, bits_per_sample %d",
            output_type, channels, bits_per_sample);
        *outbuf_len = 0;
        *bytes_written = inbuf_len;
        return ESP_FAIL;
    }
    *outbuf_len = o;
    if (bits_per_sample == 32) {
        *bytes_written = i * 4;
    } else if (bits_per_sample == 16) {
        *bytes_written = i * 2;
    } else {
        *bytes_written = i;
    }
    return ESP_OK;
}

static esp_err_t audio_dac_write(dac_stream_t *handle, uint8_t *inbuf, size_t inbuf_len, size_t *bytes_written, TickType_t ticks_to_wait)
{
    esp_err_t res = ESP_OK;
    AUDIO_NULL_CHECK(TAG, inbuf, return ESP_FAIL);

    if (!handle->dac_handle) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Just wait a little and maybe they initialize the DAC by then
        if (!handle->dac_handle) {
            ESP_LOGW(TAG, "%s:%d (%s): AUDIO DAC CAN NOT WRITE DATA, WHEN AUDIO DAC STATUS IS %d", __FILENAME__, __LINE__, __FUNCTION__, handle->dac_handle);
            // Just discard the data
            *bytes_written = inbuf_len;
            return ESP_OK;
        }
    }

    *bytes_written = 0;
    uint8_t *out = handle->data;
    uint8_t *in = inbuf;

    while (inbuf_len) {
        size_t out_bytes = handle->config.dac_config.buffer_size;
        size_t bytes_converted = 0;
        dac_data_convert(out, &out_bytes, in, inbuf_len, &bytes_converted, handle->bits_per_sample, handle->channel_set_num, handle->config.dac_config.output_type);

        size_t bytes_loaded = 0;
        // TODO check if all bytes are loaded and handle the case where they are not
        res = dac_continuous_write(handle->dac_handle, out, out_bytes, &bytes_loaded, -1);
        ESP_LOGD(TAG, "audio_dac_write err %d inbuf_len %d bytes_loaded: %d  %-3d %-3d %-3d",
            res, inbuf_len, bytes_loaded,  (int)inbuf[0], (int)inbuf[1], (int)inbuf[2]);

        *bytes_written += bytes_converted;
        in += bytes_converted;
        inbuf_len -= bytes_converted;
    }
    return res;
}

static esp_err_t audio_dac_start(dac_stream_t *handle)
{
    esp_err_t res = ESP_OK;
    ESP_LOGD(TAG, "Start audio dac");

    if (handle->dac_handle) {
        ESP_LOGE(TAG, "%s:%d (%s): AUDIO DAC STATE IS %p, AND SHOULD BE IDLE WHEN DAC START", 
            __FILENAME__, __LINE__, __FUNCTION__, handle->dac_handle);
    }
    dac_continuous_config_t cont_cfg = {
        .chan_mask = (handle->config.dac_config.enable_left ? DAC_CHANNEL_MASK_CH0 : 0) | (handle->config.dac_config.enable_right ? DAC_CHANNEL_MASK_CH1 : 0),
        .desc_num = 4,
        .buf_size = handle->config.dac_config.buffer_size,
        .freq_hz = handle->framerate,
        .offset = -128,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,   // Using APLL as clock source to get a wider frequency range
        /* Assume the data in buffer is 'A B C D E F'
         * DAC_CHANNEL_MODE_SIMUL:
         *      - channel 0: A B C D E F
         *      - channel 1: A B C D E F
         * DAC_CHANNEL_MODE_ALTER:
         *      - channel 0: A C E
         *      - channel 1: B D F
         */
        .chan_mode = (handle->config.dac_config.output_type == DAC_OUTPUT_TYPE_STEREO) ? 
            DAC_CHANNEL_MODE_ALTER : DAC_CHANNEL_MODE_SIMUL,
    };
    /* Allocate continuous channels */
    AUDIO_CHECK(TAG, ESP_OK == dac_continuous_new_channels(&cont_cfg, &handle->dac_handle), goto init_error, "AUDIO DAC NEW CHANNELS ERROR");
    AUDIO_CHECK(TAG, ESP_OK == dac_continuous_enable(handle->dac_handle), res = ESP_FAIL, "AUDIO DAC ENABLE ERROR");
    ESP_LOGI(TAG, "Audio DAC started with frequency %d %s", handle->framerate, 
        (handle->config.dac_config.output_type == DAC_OUTPUT_TYPE_STEREO) ? "stereo" : "mono");

init_error:
    return res;
}

static esp_err_t audio_dac_stop(dac_stream_t *handle)
{
    esp_err_t res = ESP_OK;
    if (handle->dac_handle) {
        AUDIO_CHECK(TAG, ESP_OK == dac_continuous_disable(handle->dac_handle), res = ESP_FAIL, "AUDIO DAC DISABLE ERROR");
        AUDIO_CHECK(TAG, ESP_OK == dac_continuous_del_channels(handle->dac_handle), res = ESP_FAIL, "AUDIO DAC DEL CHANNELS ERROR");
        handle->dac_handle = NULL;
    }
    ESP_LOGI(TAG, "Audio DAC stopped");
    return res;
}

static int _dac_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);
    size_t bytes_written = 0;
    audio_dac_write(dac_stream, (uint8_t *)buffer, len, &bytes_written, ticks_to_wait);
    return bytes_written;
}

static esp_err_t _dac_destroy(audio_element_handle_t self)
{
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);
    ESP_LOGI(TAG, "Destroy DAC stream");
    esp_err_t res = ESP_OK;

    res = audio_dac_stop(dac_stream);
    // Is any of this needed?
    //gpio_reset_pin(DAC_STREAM_GPIO_NUM_LEFT);
    //gpio_reset_pin(DAC_STREAM_GPIO_NUM_RIGHT);
    //gpio_set_direction(DAC_STREAM_GPIO_NUM_LEFT, GPIO_MODE_INPUT);
    //gpio_set_direction(DAC_STREAM_GPIO_NUM_RIGHT, GPIO_MODE_INPUT);
    // To prevent noise.
    //gpio_set_pull_mode(DAC_STREAM_GPIO_NUM_LEFT, GPIO_PULLUP_ONLY);
    //gpio_set_pull_mode(DAC_STREAM_GPIO_NUM_RIGHT, GPIO_PULLUP_ONLY);

    audio_free(dac_stream);
    return res;
}

static esp_err_t _dac_open(audio_element_handle_t self)
{
    ESP_LOGI(TAG, "Open DAC stream");
    esp_err_t res = ESP_OK;
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);
    if (dac_stream->is_open) {
        return ESP_OK;
    }
    res = audio_element_set_input_timeout(self, 2000 / portTICK_RATE_MS);

    dac_stream->data = heap_caps_calloc(1, dac_stream->config.dac_config.buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    AUDIO_NULL_CHECK(TAG, dac_stream->data, res = ESP_FAIL);

    res |= audio_dac_start(dac_stream);
    dac_stream->is_open = true;
    return res;
}

static esp_err_t _dac_close(audio_element_handle_t self)
{
    ESP_LOGI(TAG, "Close DAC stream");
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);
    esp_err_t res = ESP_OK;
    dac_stream->is_open = false;
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_report_pos(self);
        audio_element_set_byte_pos(self, 0);
    }
    res = audio_dac_stop(dac_stream);
    if (dac_stream->data) {
        free(dac_stream->data);
        dac_stream->data = NULL;
    }
    return res;
}

static int _dac_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    //ESP_LOGI(TAG, "Process DAC stream, in_len %d", in_len);
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);

    if (dac_stream->reinit) {
        ESP_LOGI(TAG, "Reinitializing DAC stream");
        audio_dac_stop(dac_stream);
        audio_dac_start(dac_stream);
        dac_stream->reinit = false;
    }

    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    if (r_size > 0) {
        w_size = audio_element_output(self, in_buffer, r_size);
    } else {
        w_size = r_size;
    }
    return w_size;
}

audio_element_handle_t dac_stream_init(dac_stream_cfg_t *config)
{
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();

    if (config->type != AUDIO_STREAM_WRITER) {
        ESP_LOGE(TAG, "DAC stream only support AUDIO_STREAM_WRITER mode, not support %d", config->type);
        return NULL;
    }
    cfg.open = _dac_open;
    cfg.close = _dac_close;
    cfg.process = _dac_process;
    cfg.destroy = _dac_destroy;
    cfg.write = _dac_write;
    cfg.tag = "dac";
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    //cfg.buffer_len = config->buffer_len;
    cfg.stack_in_ext = config->ext_stack;

    dac_stream_t *dac_stream = audio_calloc(1, sizeof(dac_stream_t));
    AUDIO_NULL_CHECK(TAG, dac_stream, return NULL);
    memcpy(&dac_stream->config, config, sizeof(dac_stream_cfg_t));

    audio_element_handle_t el = audio_element_init(&cfg);
    audio_element_setdata(el, dac_stream);

    // Initialize DAC in open
    audio_dac_set_param(dac_stream, 8000, 16, 2); // TODO defaults

    ESP_LOGD(TAG, "dac_stream_init init,el:%p", el);
    return el;
}

esp_err_t dac_stream_set_clk(audio_element_handle_t self, int rate, int bits, int ch)
{
    dac_stream_t *dac_stream = (dac_stream_t *)audio_element_getdata(self);
    esp_err_t res = audio_dac_set_param(dac_stream, rate, bits, ch);
    return res;
}

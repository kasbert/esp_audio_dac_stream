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

#ifndef _DAC_STREAM_H_
#define _DAC_STREAM_H_

#include "driver/dac_continuous.h"
//#include "driver/timer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Hardware output
typedef enum {
    DAC_OUTPUT_TYPE_STEREO,      /*!< Separated left and right channel (stereo mode) */
    DAC_OUTPUT_TYPE_MONO_MIX,    /*!< Mix right and left channel data to both channels (mono mode) */
    DAC_OUTPUT_TYPE_MONO_RIGHT,  /*!< Only load right channel to both channels (mono mode) */
    DAC_OUTPUT_TYPE_MONO_LEFT,   /*!< Only load left channel to both channels (mono mode) */
} dac_output_type_t;

/**
 * @brief      DAC audio configurations
 *           
 */
typedef struct
{
    // By default the data is written to both channels in mono mode
    bool enable_right;        /*!< Enable output on right channel */
    bool enable_left;         /*!< Enable output on left channel */
    dac_output_type_t output_type; /*!< Output type */
    uint32_t buffer_size;             /*!< buffer size */
} audio_dac_config_t;

/**
 * @brief      DAC Stream configurations
 *             Default value will be used if any entry is zero
 */
typedef struct {
    audio_stream_type_t     type;               /*!< Type of stream */
    audio_dac_config_t      dac_config;         /*!<  driver configurations */
    int                     task_stack;         /*!< Task stack size */
    int                     task_core;          /*!< Task running in core (0 or 1) */
    int                     task_prio;          /*!< Task priority (based on freeRTOS priority) */
    int                     buffer_len;         /*!< dac_stream buffer length */
    bool                    ext_stack;          /*!< Allocate stack on extern ram */
} dac_stream_cfg_t;

/*
Left:
enumerator DAC_CHANNEL_MASK_CH0
DAC channel 0 is GPIO25(ESP32) / GPIO17(ESP32S2)

Right:
enumerator DAC_CHANNEL_MASK_CH1
DAC channel 1 is GPIO26(ESP32) / GPIO18(ESP32S2)
*/

#define DAC_STREAM_GPIO_NUM_LEFT  GPIO_NUM_25
#define DAC_STREAM_GPIO_NUM_RIGHT GPIO_NUM_26

#define DAC_STREAM_TASK_STACK           (3072+512)
#define DAC_STREAM_BUF_SIZE             (2048)
#define DAC_STREAM_TASK_PRIO            (23)
#define DAC_STREAM_TASK_CORE            (0)
#define DAC_CONFIG_BUFFER_SIZE          (2048)

#define DAC_STREAM_CFG_DEFAULT() {                    \
    .type = AUDIO_STREAM_WRITER,                      \
    .dac_config = {                                   \
        .enable_right = true,                         \
        .enable_left = true,                          \
        .output_type = DAC_OUTPUT_TYPE_STEREO,        \
        .buffer_size = DAC_CONFIG_BUFFER_SIZE,        \
    },                                                \
    .task_stack = DAC_STREAM_TASK_STACK,              \
    .task_core = DAC_STREAM_TASK_CORE,                \
    .task_prio = DAC_STREAM_TASK_PRIO,                \
    .buffer_len =  DAC_STREAM_BUF_SIZE,               \
    .ext_stack = false,                               \
}

/**
 * @brief      Initialize DAC stream
 *             Only support AUDIO_STREAM_READER type
 *
 * @param      config   The DAC Stream configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t dac_stream_init(dac_stream_cfg_t *config);

/**
 * @brief      Setup clock for DAC Stream, this function is only used with handle created by `dac_stream_init`
 *
 * @param[in]  dac_stream   The dac element handle
 * @param[in]  rate  Clock rate (in Hz)
 * @param[in]  bits  Audio bit width (16, 32)
 * @param[in]  ch    Number of Audio channels (1: Mono, 2: Stereo)
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t dac_stream_set_clk(audio_element_handle_t dac_stream, int rate, int bits, int ch);

#ifdef __cplusplus
}
#endif

#endif

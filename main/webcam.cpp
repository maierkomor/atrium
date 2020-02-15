/*
 *  Copyright (C) 2019-2020, Thomas Maier-Komor
 *  Atrium Firmware Package for ESP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sdkconfig.h>

#ifdef CONFIG_CAMERA

#include <esp_camera.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "HttpReq.h"
#include "HttpResp.h"
#include "log.h"
#include "webcam.h"


/*
//WROOM pin map
#define CAM_PIN_PWDN    -1 //power down is not used
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK    21
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      19
#define CAM_PIN_D2      18
#define CAM_PIN_D1       5
#define CAM_PIN_D0       4
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22
*/

//AI-Thinker ESP-CAM pin map
#define CAM_PIN_PWDN    32 //power down is not used
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22


// DevKitJ
/*
#define CAM_PIN_PWDN    -1 //power down is not used
#define CAM_PIN_RESET   2 //software reset will be performed
#define CAM_PIN_XCLK    21
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      19
#define CAM_PIN_D2      18
#define CAM_PIN_D1      5
#define CAM_PIN_D0       4
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22
*/


static char TAG[] = "webcam";


extern "C"
void webcam_setup()
{
	camera_config_t cfg = {
		.pin_pwdn  = CAM_PIN_PWDN,
		.pin_reset = CAM_PIN_RESET,
		.pin_xclk = CAM_PIN_XCLK,
		.pin_sscb_sda = CAM_PIN_SIOD,
		.pin_sscb_scl = CAM_PIN_SIOC,
		.pin_d7 = CAM_PIN_D7,
		.pin_d6 = CAM_PIN_D6,
		.pin_d5 = CAM_PIN_D5,
		.pin_d4 = CAM_PIN_D4,
		.pin_d3 = CAM_PIN_D3,
		.pin_d2 = CAM_PIN_D2,
		.pin_d1 = CAM_PIN_D1,
		.pin_d0 = CAM_PIN_D0,
		.pin_vsync = CAM_PIN_VSYNC,
		.pin_href = CAM_PIN_HREF,
		.pin_pclk = CAM_PIN_PCLK,

		//XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
		.xclk_freq_hz = 20000000,
		.ledc_timer = LEDC_TIMER_0,
		.ledc_channel = LEDC_CHANNEL_0,
		.pixel_format = PIXFORMAT_JPEG,//YUV422,GRAYSCALE,RGB565,JPEG
		.frame_size = FRAMESIZE_UXGA,//QQVGA-QXGA Do not use sizes above QVGA when not JPEG
		.jpeg_quality = 12, //0-63 lower number means higher quality
		.fb_count = 4 //if more than one, i2s runs in continuous mode.  Use only with JPEG
	};

#if CAM_PIN_PWDN != -1
	gpio_pad_select_gpio(CAM_PIN_PWDN);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)CAM_PIN_PWDN, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set %u to output: %s",CAM_PIN_PWDN,esp_err_to_name(e));
		return;
	}
	gpio_set_level((gpio_num_t)CAM_PIN_PWDN,1);
	vTaskDelay(10 / portTICK_PERIOD_MS);
	gpio_set_level((gpio_num_t)CAM_PIN_PWDN,0);
#endif
	if (esp_err_t e = esp_camera_init(&cfg))
		log_error(TAG, "init failed %s",esp_err_to_name(e));
	else
		log_info(TAG,"setup done");
}


void webcam_sendframe(HttpRequest *r)
{
	camera_fb_t * fb = esp_camera_fb_get();
	HttpResponse res;
	if (0 == fb) {
		log_warn(TAG,"capture failed");
		res.setResult(HTTP_SVC_UNAVAIL);
	} else {
		log_info(TAG,"capture ok - sending jpg");
		res.setResult(HTTP_OK);
		res.setContentType(CT_IMAGE_JPEG);
		res.addContent((const char*)fb->buf,fb->len);
	}
	res.senddata(r->getConnection());
	esp_camera_fb_return(fb);
}


#endif // CONFIG_CAMERA

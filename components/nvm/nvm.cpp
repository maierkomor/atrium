/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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

#include "log.h"
#include "nvm.h"
#include "profiling.h"

#include <errno.h>
#include <nvs.h>
#include <nvs_flash.h>


#define TAG MODULE_NVM


static nvs_handle NVS;


int nvm_setup()
{
	PROFILE_FUNCTION();
	if (nvs_flash_init()) {
		log_warn(TAG,"init failed - erasing NVS");
		if (esp_err_t e = nvs_flash_erase())
			log_warn(TAG,"erase: %s",esp_err_to_name(e));
		else if (esp_err_t e = nvs_flash_init())
			log_warn(TAG,"init: %s",esp_err_to_name(e));
	}
	if (esp_err_t err = nvs_open("cfg",NVS_READWRITE,&NVS)) {
		log_warn(TAG,"open failed: %s",esp_err_to_name(err));
		return 1;
	}
	return 0;
}


uint8_t nvm_read_u8(const char *id, uint8_t d)
{
	uint8_t v;
	if (esp_err_t e = nvs_get_u8(NVS,id,&v)) {
		log_warn(TAG,"get %s: %s",id,esp_err_to_name(e));
		return d;
	}
	return v;
}


void nvm_store_u8(const char *id, uint8_t v)
{
	if (esp_err_t e = nvs_set_u8(NVS,id,v))
		log_warn(TAG,"set %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_warn(TAG,"commit %s: %s",id,esp_err_to_name(e));
}


uint16_t nvm_read_u16(const char *id, uint16_t d)
{
	uint16_t v;
	if (esp_err_t e = nvs_get_u16(NVS,id,&v)) {
		log_warn(TAG,"get %s: %s",id,esp_err_to_name(e));
		return d;
	}
	return v;
}


void nvm_store_u16(const char *id, uint16_t v)
{
	if (esp_err_t e = nvs_set_u16(NVS,id,v))
		log_warn(TAG,"set %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_warn(TAG,"commit %s: %s",id,esp_err_to_name(e));
}


uint32_t nvm_read_u32(const char *id, uint32_t d)
{
	uint32_t v;
	if (esp_err_t e = nvs_get_u32(NVS,id,&v)) {
		log_warn(TAG,"get %s: %s",id,esp_err_to_name(e));
		return d;
	}
	return v;
}


void nvm_store_u32(const char *id, uint32_t v)
{
        if (esp_err_t e = nvs_set_u32(NVS,id,v))
                log_warn(TAG,"set %s to %u: %s",id,(unsigned)v,esp_err_to_name(e));
        else if (esp_err_t e = nvs_commit(NVS))
                log_warn(TAG,"commit %s: %s",id,esp_err_to_name(e));
}


float nvm_read_float(const char *id, float d)
{
	float v;
        if (esp_err_t e = nvs_get_u32(NVS,id,(uint32_t*)&v)) {
                log_warn(TAG,"get %s: %s",id,esp_err_to_name(e));
                return d;
        }
        return v;
}


void nvm_store_float(const char *id, float v)
{
	union {
		float f;
		uint32_t u;
	} x;
	x.f = v;
        if (esp_err_t e = nvs_set_u32(NVS,id,x.u))
                log_warn(TAG,"set %s to %f: %s",id,v,esp_err_to_name(e));
        else if (esp_err_t e = nvs_commit(NVS))
                log_warn(TAG,"commit %s: %s",id,esp_err_to_name(e));
}


int nvm_read_blob(const char *name, uint8_t **buf, size_t *len)
{
	size_t s = 0;
	if (esp_err_t e = nvs_get_blob(NVS,name,0,&s)) {
		return e;
	}
	uint8_t *b = (uint8_t*)malloc(s);
	if (b == 0)
		return ENOMEM;
	if (esp_err_t e = nvs_get_blob(NVS,name,b,&s)) {
		free(buf);
		return e;
	}
	*buf = b;
	*len = s;
	return 0;
}


int nvm_store_blob(const char *name, const uint8_t *buf, size_t s)
{
	if (esp_err_t e = nvs_set_blob(NVS,name,buf,s))
		log_warn(TAG,"write %s (%u bytes): %s",name,s,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_warn(TAG,"commit %s: %s",name,esp_err_to_name(e));
	else
		return 0;
	return 1;
}


int nvm_copy_blob(const char *to, const char *from)
{
	size_t s = 0;
	uint8_t *buf = 0;
	if (int e = nvm_read_blob(from,&buf,&s)) {
		log_warn(TAG,"read %s: %s",from,esp_err_to_name(e));
		return e;
	}
	esp_err_t e = nvs_set_blob(NVS,to,buf,s);
	free(buf);
	if (e)
		log_warn(TAG,"NVS write %s (%u bytes): %s",to,s,esp_err_to_name(e));
	else if (esp_err_t e = nvs_commit(NVS))
		log_warn(TAG,"commit %s: %s",to,esp_err_to_name(e));
	else
		return 0;
	return 1;

}


void nvm_erase_key(const char *k)
{
	if (esp_err_t e = nvs_erase_key(NVS,k))
		log_warn(TAG,"erase %s: %s",k,esp_err_to_name(e));
}


void nvm_erase_all()
{
	if (esp_err_t e = nvs_erase_all(NVS))
		log_warn(TAG,"erase all: %s",esp_err_to_name(e));
}



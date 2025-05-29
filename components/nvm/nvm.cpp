/*
 *  Copyright (C) 2017-2025, Thomas Maier-Komor
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

#include "log.h"
#include "nvm.h"
#include "profiling.h"

#include <errno.h>
#include <nvs.h>
#include <nvs_flash.h>


#define TAG MODULE_NVM

#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
#define log_dbug(...)
#else
//#define READBACK
#endif


#if IDF_VERSION >= 52
static nvs_handle_t NVS = 0;
#else
static nvs_handle NVS = 0;
typedef nvs_handle nvs_handle_t;
#endif


int nvm_setup()
{
	PROFILE_FUNCTION();
	if (0 == NVS) {
		const char *stage = 0;
		esp_err_t e;
		if (nvs_flash_init()) {
			log_warn(TAG,"init failed - erasing NVS");
			if (0 != (e = nvs_flash_erase())) {
				stage = "erase";
			} else if (0 != (e = nvs_flash_init())) {
				stage = "reinit";
			}
		}
		if (0 != (e = nvs_open("cfg",NVS_READWRITE,&NVS))) {
			stage = "open";
		}
		if (stage) {
			log_warn(TAG,"setup at %s: %s",stage,esp_err_to_name(e));
			return 1;
		}
	}
	return 0;
}


nvs_handle_t nvm_handle()
{
	if (0 == NVS)
		nvm_setup();
	return NVS;
}


uint8_t nvm_read_u8(const char *id, uint8_t d)
{
	uint8_t v;
	if (esp_err_t e = nvs_get_u8(NVS,id,&v)) {
		log_warn(TAG,"read %s: %s",id,esp_err_to_name(e));
		return d;
	}
	log_dbug(TAG,"read %s: %u",id,v);
	return v;
}


void nvm_store_u8(const char *id, uint8_t v)
{
	const char *stage;
	esp_err_t e;
#ifdef READBACK
	uint8_t s;
#endif
	if (0 != (e = nvs_set_u8(NVS,id,v))) {
		stage = "set";
	} else if (0 != (e = nvs_commit(NVS))) {
		stage = "commit";
#ifdef READBACK
	} else if (0 != (e = nvs_get_u8(NVS,id,&s))) {
		stage = "readback";
	} else if (v != s) {
		stage = "verify";
		e = ESP_ERR_NOT_FINISHED;
#endif
	} else {
		log_info(TAG,"stored %s: %u",id,v);
		return;
	}
	log_warn(TAG,"%s %s: %s",stage,id,esp_err_to_name(e));
}


uint16_t nvm_read_u16(const char *id, uint16_t v)
{
	if (esp_err_t e = nvs_get_u16(NVS,id,&v)) {
		log_warn(TAG,"read %s: %s",id,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"read %s: %u",id,v);
	}
	return v;
}


void nvm_store_u16(const char *id, uint16_t v)
{
	const char *stage;
	esp_err_t e;
#ifdef READBACK
	uint16_t s;
#endif
	if (0 != (e = nvs_set_u16(NVS,id,v))) {
		stage = "set";
	} else if (0 != (e = nvs_commit(NVS))) {
		stage = "commit";
#ifdef READBACK
	} else if (0 != (e = nvs_get_u16(NVS,id,&s))) {
		stage = "readback";
	} else if (v != s) {
		stage = "verify";
		e = ESP_ERR_NOT_FINISHED;
#endif
	} else {
		log_dbug(TAG,"stored %s: %u",id,v);
		return;
	}
	log_warn(TAG,"%s %s: %s",stage,id,esp_err_to_name(e));
}


uint32_t nvm_read_u32(const char *id, uint32_t v)
{
	if (esp_err_t e = nvs_get_u32(NVS,id,&v)) {
		log_warn(TAG,"read %s: %s",id,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"read %s: %u",id,v);
	}
	return v;
}


void nvm_store_u32(const char *id, uint32_t v)
{
	const char *stage;
	esp_err_t e;
#ifdef READBACK
	uint16_t s;
#endif
        if (0 != (e = nvs_set_u32(NVS,id,v))) {
		stage = "set";
	} else if (0 != (e = nvs_commit(NVS))) {
		stage = "commit";
#ifdef READBACK
	} else if (0 != (e = nvs_get_u32(NVS,id,&s))) {
		stage = "readback";
	} else if (v != s) {
		stage = "verify";
		e = ESP_ERR_NOT_FINISHED;
#endif
	} else {
		log_dbug(TAG,"stored %s: %u",id,v);
		return;
	}
	log_warn(TAG,"%s %s: %s",stage,id,esp_err_to_name(e));
}


float nvm_read_float(const char *id, float v)
{
        if (esp_err_t e = nvs_get_u32(NVS,id,(uint32_t*)&v)) {
                log_warn(TAG,"read %s: %s",id,esp_err_to_name(e));
        } else {
		log_dbug(TAG,"read %s: %g",id,v);
	}
        return v;
}


void nvm_store_float(const char *id, float v)
{
	const char *stage;
	esp_err_t e;
	union {
		float f;
		uint32_t u;
	} x;
	x.f = v;
        if (0 != (e = nvs_set_u32(NVS,id,x.u))) {
		stage = "set";
	} else if (0 != (e = nvs_commit(NVS))) {
		stage = "commit";
	} else {
		log_dbug(TAG,"stored %s: %u",id,v);
		return;
	}
	log_warn(TAG,"%s %s: %s",stage,id,esp_err_to_name(e));
}


size_t nvm_blob_size(const char *name)
{
	size_t s;
	esp_err_t e = nvs_get_blob(NVS,name,0,&s);
	if (e) {
		log_warn(TAG,"blob size %s: %s",name,esp_err_to_name(e));
		s = 0;
	}
	return s;
}


esp_err_t nvm_read_blob(const char *name, uint8_t *buf, size_t *s)
{
	esp_err_t e = nvs_get_blob(NVS,name,buf,s);
	if (e)
		log_warn(TAG,"read blob %s: %s",name,esp_err_to_name(e));
	return e;
}


/*
esp_err_t nvm_read_blob(const char *name, uint8_t **buf, size_t *len)
{
	esp_err_t e = 0;
	uint8_t *b = 0;
	if ((0 == buf) || (0 == len)) {
		e = ESP_ERR_INVALID_ARG;
	} else if (*buf == 0) {
		size_t s = 0;
		e = nvs_get_blob(NVS,name,0,&s);
		if (0 == e) {
			b = (uint8_t*)malloc(s);
			if (b == 0) {
				e = ESP_ERR_NO_MEM;
			} else {
				*buf = b;
				*len = s;
			}
		}
	}
	if (0 == e) {
		e = nvs_get_blob(NVS,name,*buf,len);
		if ((0 != e) && b) {
			free(b);
			*buf = 0;
		}
	}
	if (0 != e) {
		log_warn(TAG,"read blob %s: %s",name,esp_err_to_name(e));
	}
	return e;
}
*/


esp_err_t nvm_store_blob(const char *id, const uint8_t *buf, size_t s)
{
	const char *stage;
	esp_err_t e;
	if (0 != (e = nvs_set_blob(NVS,id,buf,s))) {
		stage = "set";
	} else if (0 != (e = nvs_commit(NVS))) {
		stage = "commit";
	} else {
		log_info(TAG,"stored %s",id);
		return 0;
	}
	log_warn(TAG,"%s %s: %s",stage,id,esp_err_to_name(e));
	return e;
}


const char *nvm_copy_blob(const char *to, const char *from)
{
	size_t s = nvm_blob_size(from);
	if (0 == s)
		return "Does not exist.";
	uint8_t *buf = (uint8_t *) malloc(s);
	esp_err_t e = nvm_read_blob(from,buf,&s);
	if (0 == e) {
		e = nvs_set_blob(NVS,to,buf,s);
		if (0 == e) {
			e = nvs_commit(NVS);
		}
	}
	free(buf);
	if (0 == e)
		return 0;
	return esp_err_to_name(e);
}


void nvm_erase_key(const char *k)
{
	if (esp_err_t e = nvs_erase_key(NVS,k)) {
		log_warn(TAG,"erase %s: %s",k,esp_err_to_name(e));
	} else {
		log_dbug(TAG,"erased %s",k);
	}
}


void nvm_erase_all()
{
	if (esp_err_t e = nvs_erase_all(NVS)) {
		log_warn(TAG,"erase all: %s",esp_err_to_name(e));
	} else {
		log_dbug(TAG,"reset NVM");
	}
}


#if IDF_VERSION >= 52
esp_err_t nvm_iterate(void (*cb)(const char *name, nvs_type_t type, void *arg), void *arg)
{
	nvs_iterator_t iter;
	if (esp_err_t e = nvs_entry_find("nvs","cfg",NVS_TYPE_ANY,&iter))
		return e;
	nvs_entry_info_t entry;
	while (ESP_OK == nvs_entry_info(iter,&entry)) {
		cb(entry.key,entry.type,arg);
		if (nvs_entry_next(&iter))
			break;
	}
	nvs_release_iterator(iter);
	return ESP_OK;
}


esp_err_t nvm_iterate_v(void (*cb)(const char *name, nvs_type_t type, va_list), ...)
{
	nvs_iterator_t iter;
	if (esp_err_t e = nvs_entry_find("nvs","cfg",NVS_TYPE_ANY,&iter))
		return e;
	nvs_entry_info_t entry;
	va_list val;
	va_start(val,cb);
	while (ESP_OK == nvs_entry_info(iter,&entry)) {
		cb(entry.key,entry.type,val);
		if (nvs_entry_next(&iter))
			break;
	}
	va_end(val);
	nvs_release_iterator(iter);
	return ESP_OK;
}


const char *nvs_type_str(nvs_type_t t)
{
	switch (t) {
	default:
		return "<unkown>";
	case NVS_TYPE_U8:
		return "u8";
	case NVS_TYPE_I8:
		return "i8";
	case NVS_TYPE_U16:
		return "u16";
	case NVS_TYPE_I16:
		return "i16";
	case NVS_TYPE_U32:
		return "u32";
	case NVS_TYPE_I32:
		return "i32";
	case NVS_TYPE_U64:
		return "u64";
	case NVS_TYPE_I64:
		return "i64";
	case NVS_TYPE_STR:
		return "str";
	case NVS_TYPE_BLOB:
		return "blob";
	case NVS_TYPE_ANY:
		return "any";
	}
}
#endif

function track_switch(tr)
	log_info('track_switch '..tr)
	local tid = tmr_getid(tr)
	if tid == 0 then
		tid = tmr_create(tr,250)
		event_attach(tr..'`timeout','lua!run','track_switch_off("'..tr..'")')
	end
	gpio_set(tr,1)
	tmr_start(tr)
end

function track_switch_off(tr)
	log_info('switch_off '..tr)
	gpio_set(tr,0)
end


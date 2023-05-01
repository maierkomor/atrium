function track_switch(tr)
	log_info('track_switch '..tr)
	local tidn = string.sub(tr,1,-2)
	local tid = tmr_getid(tidn)
	if tid == 0 then
		tid = tmr_create(tidn,250)
		print("timer "..tid)
		event_attach(tidn..'`timeout','lua!run','track_switch_off("'..tidn..'")')
		print("attached"..tidn)
	end
	gpio_set(tr,1)
	tmr_start(tid)
end

function track_switch_off(tr)
	log_info('switch_off '..tr)
	gpio_set(tr..'a',0)
	gpio_set(tr..'b',0)
end

function track_init()
	if track_init_state == nil then
		track_init_state = 1
		init_tmr = tmr_create('tracktmr',500)
		event_attach('tracktmr`timeout','lua!run','track_init')
	else
		track_init_state = track_init_state + 1
	end
	local track
	if track_init_state <= 8 then
		track = "w"..track_init_state.."a"
	elseif track_init_state <= 16 then
		track = "w"..(track_init_state-8).."b"
	else
		return
	end
	print(track)
	track_switch(track)
	tmr_start(init_tmr)
end

--for i=1,8 do
--	track_switch("w"..i.."a")
--end

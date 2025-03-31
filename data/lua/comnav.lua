com_stb_dref="sim/cockpit/radios/com1_stdby_freq_hz"
com_act_dref="sim/cockpit/radios/com1_freq_hz"
nav_stb_dref="sim/cockpit/radios/nav1_stdby_freq_hz"
nav_act_dref="sim/cockpit/radios/nav1_freq_hz"
com_values = {0,20,50,70}

com_act = 0
com_stb = 0
nav_act = 0
nav_stb = 0

com_act_h = 120
com_act_l = 050
com_stb_h = 120
com_stb_l = 050
nav_stb_h = 110
nav_stb_l = 850
nav_act_h = 110
nav_act_l = 550

com_freq_min = 118
com_freq_max = 136
com_step = 10
nav_freq_min = 108
nav_freq_max = 117
nav_step = 50

yield = 0
timeout = 0

led_set('fault','on')

ht16k33_setnum(0,12)
ht16k33_setnum(1,12)
ht16k33_write(0,'404040404040404040404040')
ht16k33_write(1,'404040404040404040404040')

event_attach('xplane`ready','lua!run','comnav_update()')
event_attach('xplane`update','lua!run','comnav_update()')

comnav_on = false
if nil == var_get("xplane.avionics_on") or nil == var_get("xplane.battery_on") then
	comnav_on = true
elseif (var_get("xplane.avionics_on") > 0) and (var_get("xplane.battery_on") > 0) then
	comnav_on = true
end
ht16k33_onoff(0,comnav_on)
ht16k33_onoff(1,comnav_on)


function comnav_onoff()
	local pwr = var_get("xplane.avionics_on") + var_get("xplane.battery_on")
	local pwr_on = (pwr >= 2)
	if pwr_on ~= comnav_on then
		ht16k33_onoff(0,pwr_on)
		ht16k33_onoff(1,pwr_on)
		comnav_on = pwr_on
	end
end

function comnav_update()
	timeout = 0
	comnav_onoff()
	if yield > 0 then
		yield = yield - 1
		return
	end
	com_act = var_get("xplane.com_act")
	if 0 == com_act or nil == com_act then
		return
	end
	com_act_h = com_act//100
	com_act_l = (com_act%100) * 10
	com_stb = var_get("xplane.com_stb")
	com_stb_h = com_stb//100
	com_stb_l = (com_stb%100) * 10
	nav_act = var_get("xplane.nav_act")
	nav_act_h = nav_act//100
	nav_act_l = (nav_act%100) * 10
	nav_stb = var_get("xplane.nav_stb")
	nav_stb_h = nav_stb//100
	nav_stb_l = (nav_stb%100) * 10
	com_update(false)
	nav_update(false)
	led_set('fault','off')
end

function comnav_timeout()
	if timeout == 1 then
		led_set('fault','on')
	end
	timeout = 1
end

function compile_freq(stb,act)
	local e = 100000;
	local disp = {}
	for i=1,5 do
		local d = stb // e
		disp[#disp+1] = string.format('%d',d)
		stb = stb - d * e
		if (i == 3) then
			disp[#disp+1] = '.'
		end
		d = act // e
		disp[#disp+1] = string.format('%d',d)
		act = act - d * e
		if (i == 3) then
			disp[#disp+1] = '.'
		end
		e = e / 10
	end
	disp[#disp+1] = string.format('%d',stb)
	disp[#disp+1] = string.format('%d',act)
	local str = table.concat(disp)
	return str
end

function com_update(send)
 	local act = com_act_h * 1000 + com_act_l
 	local stb = com_stb_h * 1000 + com_stb_l
	if send == true then
		xplane_dref(com_act_dref,act/10.0)
		xplane_dref(com_stb_dref,stb/10.0)
	end
	local str = compile_freq(stb,act)
	ht16k33_setpos(1,0)
	ht16k33_print(1,str)
end

function nav_update(send)
 	local stb = nav_stb_h * 1000 + nav_stb_l
 	local act = nav_act_h * 1000 + nav_act_l
	if send == true then
		xplane_dref(nav_act_dref,act/10.0)
		xplane_dref(nav_stb_dref,stb/10.0)
	end
	local str = compile_freq(stb,act)
	ht16k33_setpos(0,0)
 	ht16k33_print(0,str)
end

function com_swap()
	local x = com_stb_h
	com_stb_h = com_act_h
	com_act_h = x
	x = com_stb_l
	com_stb_l = com_act_l
	com_act_l = x
	com_update(true)
	yield = 2
end

function nav_swap()
	local x = nav_stb_h
	nav_stb_h = nav_act_h
	nav_act_h = x
	x = nav_stb_l
	nav_stb_l = nav_act_l
	nav_act_l = x
	nav_update(true)
	yield = 2
end

function wrap_com_low(x)
	if x < 0 then
		x = 1000-com_step
	elseif x >= 1000 then
		x = 0
	end
	return x
end

function wrap_com_high(x)
	if x < com_freq_min then
		x = com_freq_max
	elseif x > com_freq_max then
		x = com_freq_min
	end
	return x
end

function com_a_left()
	com_stb_h = wrap_com_high(com_stb_h - 1)
	com_update(true)
end
function com_a_right()
	com_stb_h = wrap_com_high(com_stb_h + 1)
	com_update(true)
end
function com_b_left()
	com_stb_l = wrap_com_low(com_stb_l - com_step)
	local m = com_stb_l%100
	if (40 == m) or (45 == m) or (70 == m) or (95 == m) then
		com_stb_l = com_stb_l - com_step
	end
	com_update(true)
end
function com_b_right()
	com_stb_l = wrap_com_low(com_stb_l + com_step)
	local m = com_stb_l%100
	if (40 == m) or (45 == m) or (70 == m) or (95 == m) then
		com_stb_l = com_stb_l + com_step
	end
	com_update(true)
end

function wrap_nav_low(x)
	if x < 0 then
		x = 1000-nav_step
	elseif x >= 1000 then
		x = 0
	end
	return x
end

function wrap_nav_high(x)
	if x < nav_freq_min then
		x = nav_freq_max
	elseif x > nav_freq_max then
		x = nav_freq_min
	end
	return x
end

function nav_a_left()
	nav_stb_h = wrap_nav_high(nav_stb_h - 1)
	nav_update(true)
end
function nav_a_right()
	nav_stb_h = wrap_nav_high(nav_stb_h + 1)
	nav_update(true)
end
function nav_b_left()
	nav_stb_l = wrap_nav_low(nav_stb_l - nav_step)
	nav_update(true)
end
function nav_b_right()
	nav_stb_l = wrap_nav_low(nav_stb_l + nav_step)
	nav_update(true)
end


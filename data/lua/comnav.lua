comnav=1

if comnav == 1 then
	com_stb_dref="sim/cockpit/radios/com1_stdby_freq_hz"
	com_act_dref="sim/cockpit/radios/com1_freq_hz"
	nav_stb_dref="sim/cockpit/radios/nav1_stdby_freq_hz"
	nav_act_dref="sim/cockpit/radios/nav1_freq_hz"
	com_crs_up="sim/radios/stby_com1_coarse_up"
	com_crs_dwn="sim/radios/stby_com1_coarse_down"
	com_fn_up="sim/radios/stby_com1_fine_up"
	com_fn_dwn="sim/radios/stby_com1_fine_down"
	nav_crs_up="sim/radios/stby_nav1_coarse_up"
	nav_crs_dwn="sim/radios/stby_nav1_coarse_down"
	nav_fn_up="sim/radios/stby_nav1_fine_up"
	nav_fn_dwn="sim/radios/stby_nav1_fine_down"
elseif comnav == 2 then
	com_stb_dref="sim/cockpit/radios/com2_stdby_freq_hz"
	com_act_dref="sim/cockpit/radios/com2_freq_hz"
	nav_stb_dref="sim/cockpit/radios/nav2_stdby_freq_hz"
	nav_act_dref="sim/cockpit/radios/nav2_freq_hz"
	com_crs_up="sim/radios/stby_com2_coarse_up"
	com_crs_dwn="sim/radios/stby_com2_coarse_down"
	com_fn_up="sim/radios/stby_com2_fine_up"
	com_fn_dwn="sim/radios/stby_com2_fine_down"
	nav_crs_up="sim/radios/stby_nav2_coarse_up"
	nav_crs_dwn="sim/radios/stby_nav2_coarse_down"
	nav_fn_up="sim/radios/stby_nav2_fine_up"
	nav_fn_dwn="sim/radios/stby_nav2_fine_down"
else
	return
end

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
	if comnav == 1 then
		com_act = var_get("xplane.com1act")
		com_stb = var_get("xplane.com1stb")
		nav_act = var_get("xplane.nav1act")
		nav_stb = var_get("xplane.nav1stb")
	elseif comnav == 2 then
		com_act = var_get("xplane.com2act")
		com_stb = var_get("xplane.com2stb")
		nav_act = var_get("xplane.nav2act")
		nav_stb = var_get("xplane.nav2stb")
	else
		return
	end
	if 0 == com_act or nil == com_act then
		return
	end
	com_act_h = com_act//1000
	com_act_l = com_act%1000
	com_stb_h = com_stb//1000
	com_stb_l = com_stb%1000
	nav_act_h = nav_act//100
	nav_act_l = (nav_act%100) * 10
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

function com_a_left()
	xplane_command(com_crs_dwn)
end
function com_a_right()
	xplane_command(com_crs_up)
end
function com_b_left()
	xplane_command(com_fn_dwn)
end
function com_b_right()
	xplane_command(com_fn_up)
end
function nav_a_left()
	xplane_command(nav_crs_dwn)
end
function nav_a_right()
	xplane_command(nav_crs_up)
end
function nav_b_left()
	xplane_command(nav_fn_dwn)
end
function nav_b_right()
	xplane_command(nav_fn_up)
end


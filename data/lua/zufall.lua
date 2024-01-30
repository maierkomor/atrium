if leds == nil then
	zcolors = { 0xff0000, 0xff00, 0xff, 0xffff00, 0xff00ff, 0xffff , 0x80ff00, 0x80ff, 0xff80, 0xff0080, 0xff8000, 0x80ff, 0xc0ff80, 0x80c0ff, 0xff80c0, 0x80ffc0, 0xffffff }
	num = rgbleds_num()
	leds = {}
	local i
	for i = 1,num,1 do
		leds[i] = 0
	end
	iter = 0
end

if iter % 2 == 0 then
	for i = 1,num,1 do
		local v = leds[i]
		if v > 0 then
			v = (v >> 1) & 0x7f7f7f
			leds[i] = v
		end
	end
end
iter = iter + 1
leds[(math.ceil(math.random()*num))] = zcolors[(math.ceil(math.random()*#zcolors))]
leds[(math.ceil(math.random()*num))] = zcolors[(math.ceil(math.random()*#zcolors))]
leds[(math.ceil(math.random()*num))] = zcolors[(math.ceil(math.random()*#zcolors))]
leds[(math.ceil(math.random()*num))] = zcolors[(math.ceil(math.random()*#zcolors))]
rgbleds_write(leds)

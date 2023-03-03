if args == nil then
	colors = { 0xff0000, 0xff00, 0xff, 0xffff00, 0xff00ff, 0xffff , 0x80ff00, 0x80ff, 0xff80, 0xff0080, 0xff8000, 0x80ff }
	args = {}
	num = rgbleds_num()
	-- bg = 0x404040
	bg = 0
	drops = {}
	nd = 0
	x = 0
end

function paint_drop(drop,data)

end

function create_drop()
	local drop
	drop.pos = random() % num
	drop.maxsize = (random() % 7) + 1
	drop.steps = (random() % 40) + 10
	drop.step = 0
	drop.color = colors[random()%#colors]
	drops[#drops+1] = drop
end

function iterate_drops()
	for d in ipairs(drops) do

	end
end



for i = 0,num-1,1 do
	args[i] = bg
end


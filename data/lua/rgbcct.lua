if colors == nil then
	var_new('color','none');
	colors = {
		["rot"] = {255,0,0},
		["limette"] = {0,255,0},
		["blau"] = {0,0,255},
		["gelb"] = {255,255,0},
		["cyan"] = {0,255,255},
		["magenta"] = {255,0,255},
		["olive"] = {128,128,0},
		["grün"] = {0,128,0},
		["lila"] = {128,0,128},
		["blaugrün"] = {0,128,128},
		["marine"] = {0,0,128},
		["dunkelrot"] = {139,0,0},
		["braun"] = {165, 42, 42},
		["schamottestein"] = {178, 34, 34},
		["purpur"] = {220, 20, 60},
		["koralle"] = {255, 127, 80},
		["indisch rot"] = {205,92,92},
		["lachs"] = {250, 128, 114},
		["orange rot"] = {255,69,0},
		["dunkelorange"] = {255, 140, 0},
		["orange"] = {255,165,0},
		["gold"] = {255,215,0},
		["gelbgrün"] = {154, 205, 50},
		["dunkelolivgrün"] = {85, 107, 47},
		["olivgrün"] = {107, 142, 35},
		["rasen grün"] = {124,252,0},
		["grün gelb"] = {173, 255, 47},
		["dunkelgrün"] = {0,100,0},
		["waldgrün"] = {34, 139, 34},
		["lindgrün"] = {50, 205, 50},
		["hellgrün"] = {144, 238, 144},
		["dunkles seegrün"] = {143, 188, 143},
		["frühlingsgrün"] = {0,255,127},
		["meeresgrün"] = {46, 139, 87},
		["medium aqua marine"] = {102, 205, 170},
		["mittleres seegrün"] = {60, 179, 113},
		["dunkles cyan"] = {0,139,139},
		["aqua"] = {0,255,255},
		["dunkles türkis"] = {0,206,209},
		["türkis"] = {64, 224, 208},
		["mittleres türkis"] = {72, 209, 204},
		["aqua marine"] = {127, 255, 212},
		["kadettenblau"] = {95, 158, 160},
		["stahlblau"] = {70, 130, 180},
		["kornblumenblau"] = {100, 149, 237},
		["tiefes himmelblau"] = {0,191,255},
		["dodger blau"] = {30, 144, 255},
		["himmelblau"] = {135, 206, 235},
		["mitternachtsblau"] = {25,25,112},
		["mittelblau"] = {0,0,205},
		["königsblau"] = {65, 105, 225},
		["blau violett"] = {138, 43, 226},
		["indigo"] = {75,0,130},
		["dunkles schieferblau"] = {72, 61, 139},
		["schieferblau"] = {106,90,205},
		["mittelschieferblau"] = {123, 104, 238},
		["mittelviolett"] = {147, 112, 219},
		["dunkles magenta"] = {139,0,139},
		["dunkelviolett"] = {148,0,211},
		["orchidee"] = {153,50,204},
		["pflaume"] = {221,160,221},
		["violett"] = {238, 130, 238},
		["mittelviolett rot"] = {199, 21, 133},
		["hellviolettrot"] = {219,112,147},
		["dunkelrosa"] = {255, 20, 147},
		["pink"] = {255, 105, 180},
		["sienna"] = {160,82,45},
		["peru"] = {205, 133, 63},
	}
end

numcolors = 0
colornames = {}
for k,v in pairs(colors) do
	numcolors = numcolors + 1
	colornames[numcolors] = k
end
h,m = time()


function color_set(arg)
	local r,g,b,c,w
	if (string.sub(arg,1,1) == '#') then
		r = tonumber(string.sub(arg,2,3),16)
		g = tonumber(string.sub(arg,4,5),16)
		b = tonumber(string.sub(arg,6,7),16)
		c,w = 0,0
	elseif arg == "red" then
		r,g,b,c,w = 1023,0,0,0,0
	elseif arg == "blue" then
		r,g,b,c,w = 0,0,1023,0,0
	elseif arg == "green" then
		r,g,b,c,w = 0,1023,0,0,0
	elseif arg == "cyan" then
		r,g,b,c,w = 0,1023,1023,0,0
	elseif arg == "yellow" then
		r,g,b,c,w = 1023,1023,0,0,0
	elseif arg == "white" then
		r,g,b,c,w = 1023,1023,1023,0,0
	elseif arg == "coldwhite" then
		r,g,b,c,w = 0,0,0,1023,0
	elseif arg == "warmwhite" then
		r,g,b,c,w = 0,0,0,0,1023
	elseif arg == "fullwhite" then
		r,g,b,c,w = 0,0,0,1023,1023
	elseif arg == "max" then
		r,g,b,c,w = 1023,1023,1023,1023,1023
	elseif arg == "brightwhite" then
		r,g,b,c,w = 1023,1023,1023,1023,1023
	elseif arg == "navy" then
		r,g,b,c,w = 0,0,512,0,0
	else
		if colors[arg] then
			r = colors[arg][1]*4
			g = colors[arg][2]*4
			b = colors[arg][3]*4
			c = 0
			w = 0
		else 
			log_info("unknown color")
			return
		end
	end
	var_set('color',arg);
	dimmer_set('red',r)
	dimmer_set('green',g)
	dimmer_set('blue',b)
	dimmer_set('cw',c)
	dimmer_set('ww',w)
end


function color_rnd()
	color_set(colornames[math.random(numcolors)])
end


function color_manual(arg)
	sm_set('led','manual')
	color_set(mqtt_get(var_get('node')..'/set_color'))
end

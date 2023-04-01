if colors == nil then
	var_new('color','none');
	colors = {
		["schwarz"] = {0,0,0},
		["weiß"] = {255,255,255},
		["rot"] = {255,0,0},
		["limette"] = {0,255,0},
		["blau"] = {0,0,255},
		["gelb"] = {255,255,0},
		["cyan / aqua"] = {0,255,255},
		["magenta / fuchsia"] = {255,0,255},
		["silber"] = {192, 192, 192},
		["grau"] = {128, 128, 128},
		["kastanienbraun"] = {128,0,0},
		["olive"] = {128,128,0},
		["grün"] = {0,128,0},
		["lila"] = {128,0,128},
		["blaugrün"] = {0,128,128},
		["marine"] = {0,0,128},
		["kastanienbraun"] = {128,0,0},
		["dunkelrot"] = {139,0,0},
		["braun"] = {165, 42, 42},
		["schamottestein"] = {178, 34, 34},
		["purpur"] = {220, 20, 60},
		["rot"] = {255,0,0},
		["tomate"] = {255,99,71},
		["koralle"] = {255, 127, 80},
		["indisch rot"] = {205,92,92},
		["leichte koralle"] = {240, 128, 128},
		["dunkler lachs"] = {233, 150, 122},
		["lachs"] = {250, 128, 114},
		["leichter lachs"] = {255, 160, 122},
		["orange rot"] = {255,69,0},
		["dunkelorange"] = {255, 140, 0},
		["orange"] = {255,165,0},
		["gold"] = {255,215,0},
		["dunkelgoldener stab"] = {184, 134, 11},
		["goldener stab"] = {218, 165, 32},
		["blassgoldener stab"] = {238,232,170},
		["dunkles khaki"] = {189, 183, 107},
		["khaki"] = {240, 230, 140},
		["olive"] = {128,128,0},
		["gelb"] = {255,255,0},
		["gelbgrün"] = {154, 205, 50},
		["dunkelolivgrün"] = {85, 107, 47},
		["olivgrün"] = {107, 142, 35},
		["rasen grün"] = {124,252,0},
		["wiederverwendung von diagrammen"] = {127,255,0},
		["grün gelb"] = {173, 255, 47},
		["dunkelgrün"] = {0,100,0},
		["grün"] = {0,128,0},
		["waldgrün"] = {34, 139, 34},
		["limette"] = {0,255,0},
		["lindgrün"] = {50, 205, 50},
		["hellgrün"] = {144, 238, 144},
		["blasses grün"] = {152, 251, 152},
		["dunkles seegrün"] = {143, 188, 143},
		["mittleres frühlingsgrün"] = {0,250,154},
		["frühlingsgrün"] = {0,255,127},
		["meeresgrün"] = {46, 139, 87},
		["medium aqua marine"] = {102, 205, 170},
		["mittleres seegrün"] = {60, 179, 113},
		["hell meergrün"] = {32, 178, 170},
		["dunkles schiefergrau"] = {47,79,79},
		["blaugrün"] = {0,128,128},
		["dunkles cyan"] = {0,139,139},
		["aqua"] = {0,255,255},
		["cyan"] = {0,255,255},
		["helles cyan"] = {224,255,255},
		["dunkles türkis"] = {0,206,209},
		["türkis"] = {64, 224, 208},
		["mittleres türkis"] = {72, 209, 204},
		["blass türkis"] = {175, 238, 238},
		["aqua marine"] = {127, 255, 212},
		["puderblau"] = {176, 224, 230},
		["kadettenblau"] = {95, 158, 160},
		["stahlblau"] = {70, 130, 180},
		["kornblumenblau"] = {100, 149, 237},
		["tiefes himmelblau"] = {0,191,255},
		["dodger blau"] = {30, 144, 255},
		["hellblau"] = {173, 216, 230},
		["himmelblau"] = {135, 206, 235},
		["hell himmelblau"] = {135, 206, 250},
		["mitternachtsblau"] = {25,25,112},
		["marine"] = {0,0,128},
		["dunkelblau"] = {0,0,139},
		["mittelblau"] = {0,0,205},
		["blau"] = {0,0,255},
		["königsblau"] = {65, 105, 225},
		["blau violett"] = {138, 43, 226},
		["indigo"] = {75,0,130},
		["dunkles schieferblau"] = {72, 61, 139},
		["schieferblau"] = {106,90,205},
		["mittelschieferblau"] = {123, 104, 238},
		["mittelviolett"] = {147, 112, 219},
		["dunkles magenta"] = {139,0,139},
		["dunkelviolett"] = {148,0,211},
		["dunkle orchidee"] = {153,50,204},
		["mittlere orchidee"] = {186,85,211},
		["lila"] = {128,0,128},
		["distel"] = {216, 191, 216},
		["pflaume"] = {221,160,221},
		["violett"] = {238, 130, 238},
		["magenta / fuchsia"] = {255,0,255},
		["orchidee"] = {218,112,214},
		["mittelviolett rot"] = {199, 21, 133},
		["hellviolettrot"] = {219,112,147},
		["dunkelrosa"] = {255, 20, 147},
		["pink"] = {255, 105, 180},
		["hell-pink"] = {255, 182, 193},
		["rosa"] = {255, 192, 203},
		["altweiß"] = {250, 235, 215},
		["beige"] = {245, 245, 220},
		["biskuit"] = {255,228,196},
		["blanchierte mandel"] = {255,235,205},
		["weizen"] = {245,222,179},
		["maisseide"] = {255,248,220},
		["zitronen-chiffon"] = {255, 250, 205},
		["hellgoldener stab gelb"] = {250, 250, 210},
		["hellgelb"] = {255, 255, 224},
		["sattel braun"] = {139,69,19},
		["sienna"] = {160,82,45},
		["peru"] = {205, 133, 63},
		["kräftiges holz"] = {222, 184, 135},
		["tan"] = {210, 180, 140},
		["rosigbraun"] = {188, 143, 143},
		["mokassin"] = {255,228,181},
		["navajo weiß"] = {255,222,173},
		["pfirsich-blätterteig"] = {255,218,185},
		["neblige rose"] = {255,228,225},
		["lavendel erröten"] = {255,240,245},
		["leinen-"] = {250, 240, 230},
		["alte spitze"] = {253, 245, 230},
		["papaya-peitsche"] = {255,239,213},
		["muschel"] = {255, 245, 238},
		["minzcreme"] = {245,255,250},
		["schiefer grau"] = {112, 128, 144},
		["hell schiefergrau"] = {119, 136, 153},
		["hellstahlblau"] = {176, 196, 222},
		["lavendel"] = {230,230,250},
		["blumenweiß"] = {255, 250, 240},
		["alice blau"] = {240, 248, 255},
		["geist weiß"] = {248,248,255},
		["honigtau"] = {240, 255, 240},
		["elfenbein"] = {255, 255, 240},
		["azurblau"] = {240, 255, 255},
		["schnee"] = {255, 250, 250},
		["dunkelgrau / dunkelgrau"] = {105, 105, 105},
		["grau / grau"] = {128, 128, 128},
		["dunkelgrau / dunkelgrau"] = {169, 169, 169},
		["silber-"] = {192, 192, 192},
		["hellgrau / hellgrau"] = {211,211,211},
		["gainsboro"] = {220,220,220},
		["weißer rauch"] = {245, 245, 245},
	--	["weiß"] = {255,255,255},
	--	["maroon"] = {128,0,0},
	--	["dark red"] = {139,0,0},
	--	["brown"] = {165,42,42},
	--	["firebrick"] = {178,34,34},
	--	["crimson"] = {220,20,60},
	--	["red"] = {255,0,0},
	--	["tomato"] = {255,99,71},
	--	["coral"] = {255,127,80},
	--	["indian red"] = {205,92,92},
	--	["light coral"] = {240,128,128},
	--	["dark salmon"] = {233,150,122},
	--	["salmon"] = {250,128,114},
	--	["light salmon"] = {255,160,122},
	--	["orange red"] = {255,69,0},
	--	["dark orange"] = {255,140,0},
	--	["orange"] = {255,165,0},
	--	["gold"] = {255,215,0},
	--	["dark golden rod"] = {184,134,11},
	--	["golden rod"] = {218,165,32},
	--	["pale golden rod"] = {238,232,170},
	--	["dark khaki"] = {189,183,107},
	--	["khaki"] = {240,230,140},
	--	["olive"] = {128,128,0},
	--	["yellow"] = {255,255,0},
	--	["yellow green"] = {154,205,50},
	--	["dark olive green"] = {85,107,47},
	--	["olive drab"] = {107,142,35},
	--	["lawn green"] = {124,252,0},
	--	["chartreuse"] = {127,255,0},
	--	["green yellow"] = {173,255,47},
	--	["dark green"] = {0,100,0},
	--	["green"] = {0,128,0},
	--	["forest green"] = {34,139,34},
	--	["lime"] = {0,255,0},
	--	["lime green"] = {50,205,50},
	--	["light green"] = {144,238,144},
	--	["pale green"] = {152,251,152},
	--	["dark sea green"] = {143,188,143},
	--	["medium spring green"] = {0,250,154},
	--	["spring green"] = {0,255,127},
	--	["sea green"] = {46,139,87},
	--	["medium aqua marine"] = {102,205,170},
	--	["medium sea green"] = {60,179,113},
	--	["light sea green"] = {32,178,170},
	--	["dark slate gray"] = {47,79,79},
	--	["teal"] = {0,128,128},
	--	["dark cyan"] = {0,139,139},
	--	["aqua"] = {0,255,255},
	--	["cyan"] = {0,255,255},
	--	["light cyan"] = {224,255,255},
	--	["dark turquoise"] = {0,206,209},
	--	["turquoise"] = {64,224,208},
	--	["medium turquoise"] = {72,209,204},
	--	["pale turquoise"] = {175,238,238},
	--	["aqua marine"] = {127,255,212},
	--	["powder blue"] = {176,224,230},
	--	["cadet blue"] = {95,158,160},
	--	["steel blue"] = {70,130,180},
	--	["corn flower blue"] = {100,149,237},
	--	["deep sky blue"] = {0,191,255},
	--	["dodger blue"] = {30,144,255},
	--	["light blue"] = {173,216,230},
	--	["sky blue"] = {135,206,235},
	--	["light sky blue"] = {135,206,250},
	--	["midnight blue"] = {25,25,112},
	--	["navy"] = {0,0,128},
	--	["dark blue"] = {0,0,139},
	--	["medium blue"] = {0,0,205},
	--	["blue"] = {0,0,255},
	--	["royal blue"] = {65,105,225},
	--	["blue violet"] = {138,43,226},
	--	["indigo"] = {75,0,130},
	--	["dark slate blue"] = {72,61,139},
	--	["slate blue"] = {106,90,205},
	--	["medium slate blue"] = {123,104,238},
	--	["medium purple"] = {147,112,219},
	--	["dark magenta"] = {139,0,139},
	--	["dark violet"] = {148,0,211},
	--	["dark orchid"] = {153,50,204},
	--	["medium orchid"] = {186,85,211},
	--	["purple"] = {128,0,128},
	--	["thistle"] = {216,191,216},
	--	["plum"] = {221,160,221},
	--	["violet"] = {238,130,238},
	--	["magenta / fuchsia"] = {255,0,255},
	--	["orchid"] = {218,112,214},
	--	["medium violet red"] = {199,21,133},
	--	["pale violet red"] = {219,112,147},
	--	["deep pink"] = {255,20,147},
	--	["hot pink"] = {255,105,180},
	--	["light pink"] = {255,182,193},
	--	["pink"] = {255,192,203},
	--	["antique white"] = {250,235,215},
	--	["beige"] = {245,245,220},
	--	["bisque"] = {255,228,196},
	--	["blanched almond"] = {255,235,205},
	--	["wheat"] = {245,222,179},
	--	["corn silk"] = {255,248,220},
	--	["lemon chiffon"] = {255,250,205},
	--	["light golden rod yellow"] = {250,250,210},
	--	["light yellow"] = {255,255,224},
	--	["saddle brown"] = {139,69,19},
	--	["sienna"] = {160,82,45},
	--	["chocolate"] = {210,105,30},
	--	["peru"] = {205,133,63},
	--	["sandy brown"] = {244,164,96},
	--	["burly wood"] = {222,184,135},
	--	["tan"] = {210,180,140},
	--	["rosy brown"] = {188,143,143},
	--	["moccasin"] = {255,228,181},
	--	["navajo white"] = {255,222,173},
	--	["peach puff"] = {255,218,185},
	--	["misty rose"] = {255,228,225},
	--	["lavender blush"] = {255,240,245},
	--	["linen"] = {250,240,230},
	--	["old lace"] = {253,245,230},
	--	["papaya whip"] = {255,239,213},
	--	["sea shell"] = {255,245,238},
	--	["mint cream"] = {245,255,250},
	--	["slate gray"] = {112,128,144},
	--	["light slate gray"] = {119,136,153},
	--	["light steel blue"] = {176,196,222},
	--	["lavender"] = {230,230,250},
	--	["floral white"] = {255,250,240},
	--	["alice blue"] = {240,248,255},
	--	["ghost white"] = {248,248,255},
	--	["honeydew"] = {240,255,240},
	--	["ivory"] = {255,255,240},
	--	["azure"] = {240,255,255},
	--	["snow"] = {255,250,250},
	--	["black"] = {0,0,0},
	--	["dim gray / dim grey"] = {105,105,105},
	--	["gray / grey"] = {128,128,128},
	--	["dark gray / dark grey"] = {169,169,169},
	--	["silver"] = {192,192,192},
	--	["light gray / light grey"] = {211,211,211},
	--	["gainsboro"] = {220,220,220},
	--	["white smoke"] = {245,245,245},
	--	["white"] = {255,255,255},
	}
end

numcolors = 0
colornames = {}
for k,v in pairs(colors) do
	numcolors = numcolors + 1
	colornames[numcolors] = k
end


function color_set(arg)
	local r,g,b,c,w
	--log_info("color_set "..arg)
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
	elseif arg == "magenta" then
		r,g,b,c,w = 1023,0,1023,0,0
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
	elseif arg == "olive" then
		r,g,b,c,w = 512,512,0,0,0
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
	color_set(mqtt_get('rgbcct1/set_color'))
end

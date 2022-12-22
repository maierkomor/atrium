if r == nil then
	r = 30
	g = 50
	b = 80
	LumMap = { 1<<4, 1<<5, 1<<6, 1<<7, 1<<8, 1<<9, 1<<10, 1<<11 };
end
args = {}
for i = 0,99,1 do
	rv = math.floor(((math.sin((((i+r)%100)/50)*3.14)+1)/2)*255)
	gv = math.floor(((math.sin((((i+g)%100)/50)*3.14)+1)/2)*255)
	bv = math.floor(((math.sin((((i+b)%100)/50)*3.14)+1)/2)*255)
	v = (rv << 16) | (gv << 8) | bv;
	args[i] = v
end
rgbleds_write(args)
r = r+0.7
g = g+1
b = b+1.2

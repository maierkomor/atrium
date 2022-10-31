if x == nil then
	x = 0
	action_activate('ledstrip!set','black')
end
action_activate('ledstrip!set',x..',black')
x = x+1
if x == 8 then
	x = 0
end
action_activate('ledstrip!set',x..',red')


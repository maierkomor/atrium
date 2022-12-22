if x == nil then
	x = 0
	action_activate('rgbleds!set','black')
end
action_activate('rgbleds!set',x..',black')
x = x+1
if x == 8 then
	x = 0
end
action_activate('rgbleds!set',x..',red')


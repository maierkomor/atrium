��T�
esp32c3P" 
atrium24563729884948346998#2192.168.11.42192.168.11.4:syslogBsntpJCETR1
mqtt://mqtt:1883*	ecm/state*warmwasser/state\ Nb
influx�atriumzatrium.home�
bq25601d`busoffusbsel!set_1�
bq25601d`busonusbsel!set_0�_
webtmr`timeoutinflux!rtdatamqtt!pub_rtdatabmp388!sampleinflux!sysinfohdc1080!sample�,
disp:on`enterdisptmr!startbacklight!on�"
disptmr`timeoutsm!set disp:off�
disp:off`enterbacklight!off�!
vbat.physical`lowbq25601d!off�
button0`shortscreen!action�"
sensetmr`timeoutopt3001!sample�
/flash�tftp://hazlik��
!
hdc1080/temperature
Temperatur
$
hdc1080/humidityLuftfeuchtigkeit

bmp388/pressure	Luftdruck


local timeUhrzeit
?
mqtt/ecm/stateECM(mqtt!publish ecm/action mainrelay!toggle
T
mqtt/warmwasser/state
Warmwasser/mqtt!publish warmwasser/action mainrelay!toggle2futura-lt-36:futura-lt-44�
webtmr�'�
disptmr���
sensetmr2�
webtmr�'�z
disp6
off/
xpt2046`pressedbacklight!onsm!set disp:on8
on2
xpt2046`presseddisplay!set_modedisptmr!start�
vbat.physical �ZE  �E��p��h�5�"`com1act"`com1stb"anav1act"anav1stb*+
sim/cockpit/radios/com1_freq_hzcom1freq
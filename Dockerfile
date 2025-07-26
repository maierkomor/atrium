FROM ubuntu
SHELL ["/bin/bash", "-c"]

ENV	IDF_ESP32=/opt/idf-esp32 \
	IDF_PYTHON_ENV_PATH_ESP32=/opt/venv-esp32 \
	IDF_TOOLS_PATH_ESP32=/opt/tools-esp32 \
	IDF_ESP8266=/opt/idf-esp8266 \
	IDF_ESP8266_VENV=/opt/venv-esp8266 \
	IDF_TOOLS_PATH_ESP8266=/opt/tools-esp8266 \
	patchdir=/opt/patches

RUN	apt-get update && \
	apt-get install -y apt-utils git cmake python3-pip python3-venv python3-virtualenv python3-click python3-libusb1 python-is-python3 mercurial screen vim && \
	mkdir -p "$IDF_TOOLS_PATH_ESP32" "$IDF_TOOLS_PATH_ESP8266" "$patchdir"

COPY patches $patchdir
WORKDIR /opt

# esp32

RUN	git clone --recurse-submodules https://github.com/espressif/esp-idf.git idf-esp32 && \
	virtualenv /opt/venv-esp32 && \
	export IDF_PATH=$IDF_ESP32 && \
	export IDF_PYTHON_ENV_PATH=$IDF_PYTHON_ENV_PATH_ESP32 && \
	export IDF_TOOLS_PATH=$IDF_TOOLS_PATH_ESP32 && \
	cd $IDF_PATH && \
	git reset --hard v5.2.5 && \
	git submodule deinit -f --all && \
	git submodule update --init && \
	. /opt/venv-esp32/bin/activate && \
	./install.sh && \
	tools/idf_tools.py install && \
	tools/idf_tools.py install-python-env && \
	cd components/lwip/lwip && \
	patch -t -p1 < $patchdir/esp32-lwip-v5.1.diff

# esp8266
RUN	git clone https://github.com/espressif/ESP8266_RTOS_SDK.git idf-esp8266 && \
	virtualenv /opt/venv-esp8266 && \
	. /opt/venv-esp8266/bin/activate && \
	pip3 install virtualenv && \
	export IDF_PATH=$IDF_ESP8266 && \
	export IDF_TOOLS_PATH=$IDF_TOOLS_PATH_ESP8266 && \
	cd $IDF_PATH && \
	git pull --recurse-submodule && \
	git reset --hard v3.3 && \
	git submodule deinit -f --all && \
	git switch release/v3.3 && \
	git submodule update --init && \
	bash install.sh && \
	tools/idf_tools.py install && \
	tools/idf_tools.py install-python-env && \
	patch -t -p1 < $patchdir/idf-esp8266-v3.3.diff

# RUN hg clone http://hazlik/hg/atrium

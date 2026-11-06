#!/usr/bin/make -f
all: plugins gen

plugins:
	$(MAKE) -C plugins/TuneBrew

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

gen: plugins dpf/utils/lv2_ttl_generator
	@./dpf/utils/generate-ttl.sh bin

install: all
	./install.sh

clean:
	$(MAKE) -C plugins/TuneBrew clean
	rm -rf bin build dpf/utils/lv2_ttl_generator

.PHONY: all plugins gen install clean

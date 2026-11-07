#!/usr/bin/make -f
PLUGINS = TuneBrew LevelBrew

all: plugins gen

plugins:
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p || exit 1; done

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

gen: plugins dpf/utils/lv2_ttl_generator
	@./dpf/utils/generate-ttl.sh bin

install: all
	./install.sh

clean:
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p clean; done
	rm -rf bin build dpf/utils/lv2_ttl_generator

.PHONY: all plugins gen install clean

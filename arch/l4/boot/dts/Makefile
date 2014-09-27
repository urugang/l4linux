ifeq ($(CONFIG_OF),y)

dtb-y += simple.dtb

targets += dtbs dtbs_install
targets += $(dtb-y)
endif

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files := *.dtb

dtbs_install: $(addsuffix _dtbinst_, $(dtb-y))

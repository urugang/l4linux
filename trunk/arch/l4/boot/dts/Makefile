ifeq ($(CONFIG_OF),y)

dtb-y += simple.dtb

targets += dtbs
targets += $(dtb-y)
endif

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files := *.dtb

# ------------

-include Makeconf.l4conf

DICE		= $(L4BID_DICE)
DICE_FLAGS_i386	= -Biv2 -Bpia32
DICE_FLAGS_arm  = -Bix0 -Bparm

DICE_FLAGS	= $(DICE_FLAGS_$(BASEARCH)) $(addprefix -P,$(CPPFLAGS)) \
		  -o $(obj)

# only one IDL file supported currently
IDL		:= l4xi.idl
IDL_FILE	= $(src)/$(IDL)

IDL_SERVER_C	:= $(IDL:.idl=-server.c)
IDL_SERVER_H	:= $(IDL:.idl=-server.h)
IDL_CLIENT_C	:= $(IDL:.idl=-client.c)
IDL_CLIENT_H	:= $(IDL:.idl=-client.h)
IDL_SYS_H	:= $(IDL:.idl=-sys.h)

IDL_O_FILES	:= $(IDL_SERVER_C:.c=.o) $(IDL_CLIENT_C:.c=.o)

IDL_FILES	:= $(IDL_SERVER_C) $(IDL_SERVER_H) \
		   $(IDL_CLIENT_C) $(IDL_CLIENT_H) $(IDL_SYS_H)

clean-files	:= $(IDL_FILES)

quiet_cmd_dice	= IDL     $(patsubst $(srctree)/%,%,$<)
      cmd_dice  = $(DICE) $(DICE_FLAGS) $<

$(IDL_SERVER_C): $(IDL_SERVER_H) $(IDL_SYS_H)
$(IDL_CLIENT_C): $(IDL_CLIENT_H) $(IDL_SYS_H)

$(addprefix $(obj)/,$(IDL_FILES)): $(IDL_FILE) $(DICE)
	$(call cmd,dice)

$(addprefix $(obj)/,$(IDL_O_FILES)): $(addprefix $(obj)/,$(IDL_FILES))

# ---------------------------------------

obj-y	:= $(IDL_O_FILES)

CFLAGS	+= -DCONFIG_L4_LINUX


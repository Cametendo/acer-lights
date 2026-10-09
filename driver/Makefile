obj-m := predator_lights.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
# CachyOS and other clang-built kernels need the module built with LLVM too.
LLVM_FLAG := $(shell grep -qs '^CONFIG_CC_IS_CLANG=y' $(KDIR)/.config && echo LLVM=1)

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAG) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(LLVM_FLAG) clean

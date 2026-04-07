BUILD_DIR := $(PWD)/build

obj-m += hello.o

all:
	mkdir -p $(BUILD_DIR)
	make -C /lib/modules/$(shell uname -r)/build M=$(BUILD_DIR) src=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(BUILD_DIR) src=$(PWD) clean
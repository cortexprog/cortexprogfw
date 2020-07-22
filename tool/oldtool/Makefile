APP		= tool
SRCS	= main.c cpu.c hidapi/linux/hid.c gdbServer.c scriptfiles.c
INCS	= hidapi/hidapi/hidapi.h cpu.h
CCFLAGS	= -iquote hidapi/hidapi -O2
LDFLAGS	= -ludev

$(APP): $(SRCS) $(INCS)
	gcc -o $(APP) $(SRCS) $(CCFLAGS) $(LDFLAGS) -g -ggdb3

install: $(APP) scripts
	sudo cp $(APP) /usr/bin/CortexProg
	sudo mkdir -p /usr/share/cortexprog/scripts
	sudo cp -Rvf SCRIPTS/*.swds /usr/share/cortexprog/scripts
	sudo chown root:root /usr/bin/CortexProg
	sudo chmod 755 /usr/bin/CortexProg

scripts:
	make -C SCRIPTS

clean:
	rm -f $(APP)

.PHONY: scripts clean

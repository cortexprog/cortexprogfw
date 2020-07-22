APP				 =  tool
CFLAGS			+= -O2
CC				 = gcc
LD				 = gcc
DISTR_FOLDER	 = _DISTRO_FOLDER_
DISTR_ARCHIVE	 = CortexProg_PCtool_source.zip
UDEV_STATIC      = ../../eudev-master/src/libudev/.libs/libudev.a
INSTALLABLE		?= $(APP).static	# install the static version
DEBUG			 = #-g -ggdb3


#common sources and incs
SRCS	+= main.c cpu.c scriptfiles.c memio.c script.c debugger.c utilOp.c
DEPS	+= $(wildcard *.h)

#usb support
DEPS		+= hidapi/hidapi/hidapi.h
CFLAGS		+= -iquote hidapi/hidapi
LDFLAGS_sta	+= $(UDEV_STATIC)

#operators
SRCS	+= opHelp.c opInfo.c opRead.c opUpload.c opWrite.c opErase.c opTrace.c
SRCS	+= opEraseAll.c opPower.c opDebug.c opSpeed.c opFwUpdate.o opCore.c

#"special" operator and its lua dependency
LUAPATH	= lua-5.3.4/src
LUASRCS	+= lapi.c lgc.c lvm.c ltm.c ltable.c lfunc.c ldebug.c lmem.c llex.c lobject.c lopcodes.c
LUASRCS	+= lparser.c lcode.c ldo.c lstring.c lstate.c lundump.c lctype.c linit.c lzio.c ldump.c
SRCS	+= $(wildcard $(LUAPATH)/l*lib.c)
SRCS	+= $(patsubst %.c,$(LUAPATH)/%.c,$(LUASRCS))
CFLAGS	+= -iquote $(LUAPATH) -DLUA_USE_POSIX
LDFLAGS	+= -lm
DEPS	+= $(wildcard $(LUAPATH)/*.h)
SRCS	+= opSpecial.c


#OS-specific things
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	SRCS		+= hidapi/linux/hid.c
	LDFLAGS_dyn	+= -ludev
endif
ifeq ($(UNAME_S),Darwin)
	SRCS		+= hidapi/mac/hid.c
	LDFLAGS_dyn	+= -framework IOKit -framework CoreFoundation
	CFLAGS		+= -arch i386 -arch x86_64
	LDFLAGS		+= -arch i386 -arch x86_64
endif



####### RULES #######

OBJS := $(patsubst %.c,%.o,$(SRCS))

all: $(APP)

.PHONY: scripts clean dist $(DISTR_ARCHIVE)

$(APP): $(OBJS) Makefile
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDFLAGS_dyn)

$(APP).static: $(OBJS) Makefile
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDFLAGS_sta) --static
	strip -s $@
	
%.o: %.c $(INCS) $(DEPS) Makefile
	$(CC) $(CFLAGS) $(DEBUG) -c $< -o $@

scripts:
	make -C SCRIPTS

dist: $(DISTR_ARCHIVE)	#create the package for distribution

$(DISTR_ARCHIVE):
	rm -rf $(DISTR_FOLDER) $(DISTR_ARCHIVE)
	
	#create the structure
	mkdir $(DISTR_FOLDER)
	mkdir $(DISTR_FOLDER)/CortexProg
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg/tool
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg/tool/VisualStudioProject
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg/tool/SCRIPTS
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg/tool/lua-5.3.4
	mkdir $(DISTR_FOLDER)/CortexProg/CortexProg/tool/lua-5.3.4/src
	mkdir $(DISTR_FOLDER)/CortexProg/ModulaR
	
	#copy ModulaR pieces needed for build
	cp ../../ModulaR/commsPacket.h $(DISTR_FOLDER)/CortexProg/ModulaR
	cat ../../ModulaR/speck.h | grep "#" > $(DISTR_FOLDER)/CortexProg/ModulaR/speck.h
	
	#copy/get SWD pieces needed for build
	touch $(DISTR_FOLDER)/CortexProg/CortexProg/util.h
	cat ../cortex.h | grep "#" > $(DISTR_FOLDER)/CortexProg/CortexProg/cortex.h
	cat ../wire.h | grep define | grep SWD > $(DISTR_FOLDER)/CortexProg/CortexProg/wire.h
	cp ../swdCommsPacket.h $(DISTR_FOLDER)/CortexProg/CortexProg/
	
	#copy libraries we need (lua and hidapi)
	cp -R lua-5.3.4/doc $(DISTR_FOLDER)/CortexProg/CortexProg/tool/lua-5.3.4/doc
	cp lua-5.3.4/src/*.h lua-5.3.4/src/*.c lua-5.3.4/src/*.hpp $(DISTR_FOLDER)/CortexProg/CortexProg/tool/lua-5.3.4/src
	cp -R hidapi $(DISTR_FOLDER)/CortexProg/CortexProg/tool
	
	#copy our sources and license
	cp *.c *.h Makefile $(DISTR_FOLDER)/CortexProg/CortexProg/tool
	cp LICENSE.txt $(DISTR_FOLDER)/CortexProg/
	
	#copy the build infra & a couple of example scripts 
	cp SCRIPTS/LPC13xx.10000000.m3.S $(DISTR_FOLDER)/CortexProg/CortexProg/tool/SCRIPTS/
	cp SCRIPTS/FM3.1fffc000.m3.S $(DISTR_FOLDER)/CortexProg/CortexProg/tool/SCRIPTS/
	cp SCRIPTS/Makefile $(DISTR_FOLDER)/CortexProg/CortexProg/tool/SCRIPTS/
	cp SCRIPTS/linker.lkr $(DISTR_FOLDER)/CortexProg/CortexProg/tool/SCRIPTS/
	
	#copy the windows build
	cp VisualStudioProject/CortexProg.vcproj $(DISTR_FOLDER)/CortexProg/CortexProg/tool/VisualStudioProject/
	cp VisualStudioProject/CortexProg.rc $(DISTR_FOLDER)/CortexProg/CortexProg/tool/VisualStudioProject/
	cp VisualStudioProject/icon1.ico $(DISTR_FOLDER)/CortexProg/CortexProg/tool/VisualStudioProject/
	cp VisualStudioProject/resource.h $(DISTR_FOLDER)/CortexProg/CortexProg/tool/VisualStudioProject/
	
	#build the ditribution package
	cd $(DISTR_FOLDER);zip -9 -r ../$(DISTR_ARCHIVE) CortexProg;cd ..
	rm -rf $(DISTR_FOLDER)

clean:
	rm -f $(APP) $(OBJS) $(DISTR_FOLDER)
	

install: $(INSTALLABLE) scripts
	sudo cp $(INSTALLABLE) /usr/bin/CortexProg
	sudo mkdir -p /usr/share/cortexprog/scripts
	sudo mkdir -p /usr/share/cortexprog/special
	sudo cp -Rvf SCRIPTS/*.swds /usr/share/cortexprog/scripts
	sudo cp -Rvf SPECIAL/*.lua /usr/share/cortexprog/special
	sudo chown root:root /usr/bin/CortexProg
	sudo chmod 755 /usr/bin/CortexProg
	sudo chown -R root:root /usr/share/cortexprog/scripts
	sudo chmod -R 755 /usr/share/cortexprog/scripts
	sudo chown -R root:root /usr/share/cortexprog/special
	sudo chmod -R 755 /usr/share/cortexprog/special

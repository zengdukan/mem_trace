PROJECT_ROOT:=$(shell pwd)

###设置编译器##
#cross=
CC=$(cross)gcc
CPP=$(cross)g++
AR=$(cross)ar

#####设置编译参数######
CFLAGS = -g -Wall -Wno-frame-address -fPIC -std=c11
ifdef cross
CFLAGS += 
endif

######设置库参数######
LDFLAGS =  -lpthread -ldl

#静态库名称#
LIB_NAME :=libmem_tracer.a
#动态库名称#
LIB_SO := libmem_tracer.so
#库和头文件所在目录#
LIB_DIR :=./lib

#中间文件所在目录#
OBJECT_DIR:=./objects
DEPFILE:=./deps

#头文件 #
INCLUDES = -I$(PROJECT_ROOT)/

#源文件#
#SRCS:=$(wildcard *.cpp)
#SRCS+=$(wildcard *.c)
SRCS:= mem_tracer.c

OBJECT_FILE:=$(addprefix $(OBJECT_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS)))))

.PHONY:lib clean test

lib:$(DEPFILE) libs
$(DEPFILE): $(SRCS) 
	@echo "Generating new dependency file...";
	@-rm -f $(DEPFILE)
	@for f in $(SRCS); do \
	OBJ=$(OBJECT_DIR)/`basename $$f|sed -e 's/\.cpp/\.o/' -e 's/\.c/\.o/'`; \
          echo $$OBJ: $$f>> $(DEPFILE); \
          echo '	$(CC) $(CFLAGS) $(INCLUDES)  -c -o $$@ $$^ $(LDFLAGS)'>> $(DEPFILE); \
        done
-include $(DEPFILE)

libs:$(OBJECT_FILE)
	@[ -e $(LIB_DIR) ] || mkdir $(LIB_DIR)
#	$(AR) -rcu $(LIB_DIR)/$(LIB_NAME) $(OBJECT_FILE)
	$(CC) -fPIC -shared -o $(LIB_DIR)/$(LIB_SO) $(OBJECT_FILE)

test:test.cpp
	$(CPP) -std=c11 -g test.cpp -o test -lpthread

test_cpu:test_cpu.cpp
	$(CPP) -g test_cpu.cpp -o test_cpu

hashmap_test:hashmap_test.c
	$(CC) -g -Wall hashmap_test.c -o hashmap_test

run_test:
	LD_PRELOAD=$(PROJECT_ROOT)/lib/$(LIB_SO) MEM_TRACER_START_ONSIG=10 MEM_TRACER_STOP_ONSIG=12 MEM_ENABLE=1 MEM_TRACER_UDP_PORT=12345 ./test

run_test_cpu:
	LD_PRELOAD=$(PROJECT_ROOT)/lib/$(LIB_SO) MEM_TRACER_START_ONSIG=10 MEM_TRACER_STOP_ONSIG=12 MEM_ENABLE=1 MEM_TRACER_UDP_PORT=12345 ./test_cpu 1

clean:
	$(RM) -f $(DEPFILE)
	$(RM) -f $(OBJECT_DIR)/*
	$(RM) -rf $(LIB_DIR)/*
	$(RM) -f test test_cpu hashmap_test

TARGET = mdriver
OBJS += memlib.o
OBJS += fcyc.o
OBJS += clock.o
OBJS += stree.o
OBJS += mdriver.o
OBJS += mm.o
LIBS += -lm -lrt

CC = gcc
CFLAGS += -MMD -MP # dependency tracking flags
CFLAGS += -I./
CFLAGS += -std=gnu99 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter
CFLAGS += -DDRIVER
LDFLAGS += $(LIBS)

all: CFLAGS += -O3 # release flags
all: $(TARGET)

release: clean all

debug: CFLAGS += -O0 # debug flags
debug: clean $(TARGET)

$(TARGET): $(OBJS)
	@chmod +x *.pl *.sh
	@sed -i -e 's/\r$$//g' *.pl *.sh # dos to unix
	@sed -i -e 's/\r/\n/g' *.pl *.sh # mac to unix
	-@./macro-check.pl -f mm.c
	-@./global_check.sh
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

DEPS = $(OBJS:%.o=%.d)
-include $(DEPS)

clean:
	-@rm $(TARGET) $(OBJS) $(DEPS) tput_* 2> /dev/null || true

test:
	@chmod +x *.pl *.sh
	@sed -i -e 's/\r$$//g' *.pl *.sh # dos to unix
	@sed -i -e 's/\r/\n/g' *.pl *.sh # mac to unix
	-@./driver.pl

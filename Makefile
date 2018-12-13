DEPDIR := dep
SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin
TARGET = $(BIN_DIR)/piface

$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(OBJ_DIR) >/dev/null)
$(shell mkdir -p $(BIN_DIR) >/dev/null)

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

ifneq ($(LIB_PREFIX),)
INCLUDES += -I$(LIB_PREFIX)/include
endif

LIB_PREFIX?=/usr/local
INCLUDES += -I/src
DEFINES = -D_GNU_SOURCE
LIBS=\
	-lubus \
	-lubox \
	-lpifacedigital \
	-lmcp23s17 \
	-lubusgpio

LDFLAGS ?= -L$(LIB_PREFIX)/lib -Wl,-rpath $(LIB_PREFIX)/lib
C_DEFINES=-g
CFLAGS =$(C_DEFINES) -Wall -Werror $(INCLUDES) $(DEFINES)

SRCS=$(wildcard $(SRC_DIR)/*.c)

OBJS=$(addprefix $(OBJ_DIR)/,$(notdir ${SRCS:.c=.o}))

.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	rm -rf $(TARGET) $(OBJ_DIR)/* $(DEPDIR)/*

$(TARGET): $(OBJS)
	${CC} -o $@ ${OBJS} ${LDFLAGS} ${LIBS}

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
COMPILE.cc = $(CXX) $(DEPFLAGS) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c $(DEPDIR)/%.d
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POSTCOMPILE)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(notdir $(SRCS))))


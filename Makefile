DEP_DIR := dep
SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin
TARGET = $(BIN_DIR)/piface

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP_DIR)/$*.Td

ifneq ($(LIB_PREFIX),)
INCLUDES += -I$(LIB_PREFIX)/include
endif

LIB_PREFIX ?= /usr/local
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
	rm -rf $(BIN_DIR)/* $(OBJ_DIR)/* $(DEP_DIR)/*

$(TARGET): $(OBJS) | $(BIN_DIR)
	${CC} -o $@ ${OBJS} ${LDFLAGS} ${LIBS}

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
POSTCOMPILE = @mv -f $(DEP_DIR)/$*.Td $(DEP_DIR)/$*.d && touch $@

$(BIN_DIR):
	mkdir -p $@

$(OBJ_DIR):
	mkdir -p $@

$(DEP_DIR):
	mkdir -p $@

$(DEP_DIR)/%.d: ;

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c $(DEP_DIR)/%.d | $(OBJ_DIR) $(DEP_DIR)
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POSTCOMPILE)

-include $(patsubst %,$(DEP_DIR)/%.d,$(basename $(notdir $(SRCS))))


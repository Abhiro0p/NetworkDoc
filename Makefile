CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -lsqlite3 -lreadline

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
INCLUDE_DIR = include

# Source files
COMMON_SRC = $(SRC_DIR)/common/utils.c $(SRC_DIR)/common/trie.c
NM_SRC = $(SRC_DIR)/nameserver/nm_main.c $(SRC_DIR)/nameserver/nm_db.c \
         $(SRC_DIR)/nameserver/nm_handlers.c $(SRC_DIR)/nameserver/nm_handlers2.c
SS_SRC = $(SRC_DIR)/storageserver/ss_main.c $(SRC_DIR)/storageserver/ss_handlers.c
CLIENT_SRC = $(SRC_DIR)/client/client_main.c $(SRC_DIR)/client/client_commands.c \
             $(SRC_DIR)/client/client_commands2.c

# Object files
COMMON_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(COMMON_SRC))
NM_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(NM_SRC))
SS_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SS_SRC))
CLIENT_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLIENT_SRC))

# Executables
NAMESERVER = $(BIN_DIR)/nameserver
STORAGESERVER = $(BIN_DIR)/storageserver
CLIENT = $(BIN_DIR)/client

.PHONY: all clean dirs test

all: dirs $(NAMESERVER) $(STORAGESERVER) $(CLIENT)

dirs:
	@mkdir -p $(BUILD_DIR)/common
	@mkdir -p $(BUILD_DIR)/nameserver
	@mkdir -p $(BUILD_DIR)/storageserver
	@mkdir -p $(BUILD_DIR)/client
	@mkdir -p $(BIN_DIR)
	@mkdir -p data
	@mkdir -p logs

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(NAMESERVER): $(COMMON_OBJ) $(NM_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built Name Server"

$(STORAGESERVER): $(COMMON_OBJ) $(SS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built Storage Server"

$(CLIENT): $(COMMON_OBJ) $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built Client"

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	rm -rf data logs
	@echo "Cleaned build artifacts"

test: all
	@echo "Running test suite..."
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

help:
	@echo "Docs++ Build System"
	@echo "==================="
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build all components (default)"
	@echo "  clean          - Remove build artifacts"
	@echo "  test           - Run test suite"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Components:"
	@echo "  nameserver     - Central coordinator"
	@echo "  storageserver  - File storage server"
	@echo "  client         - User client interface"

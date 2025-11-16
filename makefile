CXX      	= g++
CXXFLAGS 	= -std=c++17 -Wall -Wextra -O2 -g
LDFLAGS  	=

TARGET   	= pg_proxy

SRC_DIR  	= src
BUILD_DIR 	= build

SRCS = $(wildcard $(SRC_DIR)/*.cpp)

OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

#  Create DIR if not exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)


#  Leak test
leak: CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
leak: LDFLAGS  += -fsanitize=address -fno-omit-frame-pointer
leak:
	$(MAKE) clean
	$(MAKE) all


#  sudo valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --num-callers=20 --log-file=valgrind.log ./pg_proxy 127.0.0.1 5432 192.168.31.2 5432

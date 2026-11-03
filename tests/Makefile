# Host tests for the server modules and both ESP8266 sketches. Run: make -C tests
# Needs only a C/C++ compiler (clang or gcc); no ESP-IDF or Arduino install.

SERVER  := ../Server/main
PARKING := $(SERVER)/parking
DISPLAY := $(SERVER)/display
BUILD   := build

CFLAGS   := -std=c11 -Wall -Wextra -Werror -O1 -g -I. -Istubs -I$(PARKING) -I$(DISPLAY)
CXXFLAGS := -std=c++17 -Wall -Wextra -O1 -g -Wno-sign-compare -Wno-unused-parameter \
            -I. -Iarduino -Istubs -I$(PARKING)

PARKING_SRC := $(PARKING)/json_scan.c $(PARKING)/parking_update.c \
               $(PARKING)/parking_state.c $(PARKING)/rfid_session.c

TESTS := json_parsers rfid_session gif_frames sensor_client rfid_client

.PHONY: all clean
all: $(addprefix $(BUILD)/test_,$(TESTS))
	@fail=0; for t in $^; do ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; exit 1; fi

$(BUILD):
	mkdir -p $@

$(BUILD)/parking_%.o: $(PARKING)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

PARKING_OBJ := $(patsubst $(PARKING)/%.c,$(BUILD)/parking_%.o,$(PARKING_SRC))

$(BUILD)/test_json_parsers: test_json_parsers.c $(PARKING_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_rfid_session: test_rfid_session.c $(PARKING_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_gif_frames: test_gif_frames.c $(DISPLAY)/kaoruko_gif.c $(DISPLAY)/claude_gif.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_sensor_client: test_sensor_client.cpp ../SensorClient/SensorClient.ino $(PARKING_OBJ) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_sensor_client.cpp $(PARKING_OBJ) -o $@

$(BUILD)/test_rfid_client: test_rfid_client.cpp ../RFIDClient/RFIDClient.ino $(PARKING_OBJ) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_rfid_client.cpp $(PARKING_OBJ) -o $@

clean:
	rm -rf $(BUILD)

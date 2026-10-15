CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined \
  -I../components/dot11/include -I../components/spsc_ring/include \
  -I../components/telemetry/include -I../components/wifi_stats/include
SRC = test_core.c ../components/dot11/dot11.c ../components/spsc_ring/spsc_ring.c \
  ../components/telemetry/telemetry.c ../components/wifi_stats/wifi_stats.c

test: test_core
	./test_core
test_core: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)
clean:
	rm -f test_core golden_frame.bin

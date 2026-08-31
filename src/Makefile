CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g
LDFLAGS = -lm

# Source files
SOURCES = marching_cubes.c simple_world.c
OBJECTS = $(SOURCES:.c=.o)

# Demo program
DEMO_SOURCES = marching_cubes_demo.c
DEMO_OBJECTS = $(DEMO_SOURCES:.c=.o)

# Header files
HEADERS = marching_cubes.h simple_world.h

# Targets
all: marching_cubes_demo

# Build the demo program
marching_cubes_demo: $(DEMO_OBJECTS) $(OBJECTS)
	$(CC) $(DEMO_OBJECTS) $(OBJECTS) -o $@ $(LDFLAGS)

# Build object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f *.o marching_cubes_demo test_world.obj test_world.stl

# Install (optional)
install: marching_cubes_demo
	cp marching_cubes_demo /usr/local/bin/
	cp marching_cubes.h /usr/local/include/
	cp marching_cubes.o /usr/local/lib/

# Uninstall
uninstall:
	rm -f /usr/local/bin/marching_cubes_demo
	rm -f /usr/local/include/marching_cubes.h
	rm -f /usr/local/lib/marching_cubes.o

# Run the demo
run: marching_cubes_demo
	./marching_cubes_demo

# Show help
help:
	@echo "Available targets:"
	@echo "  all              - Build the demo program"
	@echo "  marching_cubes_demo - Build the demo program"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install to system (requires sudo)"
	@echo "  uninstall        - Remove from system (requires sudo)"
	@echo "  run              - Build and run the demo"
	@echo "  help             - Show this help message"

.PHONY: all clean install uninstall run help

# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -O2 `pkg-config --cflags opencv4`
LDFLAGS = `pkg-config --libs opencv4`

# Targets
all: finger finger_piano

finger: finger.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

finger_piano: finger_piano.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f finger finger_piano

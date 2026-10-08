CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PKG_CONFIG ?= pkg-config

GDK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gdk-pixbuf-2.0)
GDK_LIBS   := $(shell $(PKG_CONFIG) --libs gdk-pixbuf-2.0)

TARGET = bmp-builder-native
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GDK_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(GDK_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
